/*
 * DAR-391 step 1: keyboard and pointer events straight from real Apple
 * IOHIDFamily's IOHIDEventServiceUserClient, the path an X input driver
 * will use (hidd/IOHIDEventSystem use it on macOS; Apple's userspace
 * IOHIDEventSystem core itself was never published, DAR-369).
 *
 * For every IOHIDEventService it:
 *   - prints PrimaryUsagePage/PrimaryUsage;
 *   - opens the user client, type kIOHIDEventServiceUserClientType 'esuc'
 *     (xnu iokit/HID/IOHIDFamily/IOHIDEventServiceUserClient.h:26). Its
 *     initWithTask() requires the com.apple.hid.system.user-access-service
 *     entitlement (IOHIDEventServiceUserClient.cpp:242-253), which
 *     build_hid_event_smoketest.sh signs into this binary;
 *   - calls selector 0, kIOHIDEventServiceUserClientOpen (.h:29), which
 *     registers the client for the service's events (.cpp:364-393);
 *   - maps the event queue (clientMemoryForType, .cpp:138-190) and sets a
 *     Mach notification port on it (registerNotificationPort, .cpp:105-133).
 * Then it sleeps in mach_msg() on a port set until events arrive or the
 * time runs out (no polling), dequeues with IOKitUser's IODataQueueDequeue,
 * and decodes each entry: an IOHIDSystemQueueElement header
 * (IOHIDEventData.h:146-153) followed by eventCount IOHIDEventData records
 * (IOHIDEvent::readBytes, IOHIDEvent.cpp:1664-1683), keyboard/pointer/
 * button/scroll laid out as in IOKit/hid/IOHIDEventStructDefs.h.
 *
 * Usage: hid_event_smoketest [seconds]     (default 20)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IODataQueueClient.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDEventTypes.h>
#include <IOKit/hid/IOHIDEventStructDefs.h>
#include <unistd.h>
#include <mach/mach_time.h>

/* csops(2): code-signing status of this process (xnu bsd/sys/codesign.h). */
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
#define CS_OPS_STATUS            0
#define CS_OPS_ENTITLEMENTS_BLOB 7
#define CS_VALID  0x00000001
#define CS_SIGNED 0x20000000

#define kIOHIDEventServiceUserClientType 'esuc'
#define kIOHIDEventServiceUserClientOpen 0
#define kMaxServices 16

typedef struct __attribute__((packed, aligned(4))) {
    uint64_t timeStamp;
    uint64_t senderID;
    uint32_t options;
    uint32_t attributeLength;
    uint32_t eventCount;
} QueueElementHeader;

typedef struct {
    io_connect_t connect;
    IODataQueueMemory *queue;
    mach_port_t port;
    int page, usage;
} Service;

static int intProp(io_service_t s, const char *key)
{
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef v = IORegistryEntryCreateCFProperty(s, k, kCFAllocatorDefault, 0);
    int out = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &out);
    if (v) CFRelease(v);
    CFRelease(k);
    return out;
}

static double fixed(IOFixed f) { return (double)f / 65536.0; }

static int decode(const uint8_t *buf, uint32_t len, int svc)
{
    int keys = 0;
    if (len < sizeof(QueueElementHeader)) {
        printf("hid_event_smoketest: [%d] short entry (%u bytes)\n", svc, len);
        return 0;
    }
    const QueueElementHeader *h = (const QueueElementHeader *)buf;
    uint32_t off = sizeof(*h) + h->attributeLength;
    for (uint32_t i = 0; i < h->eventCount && off + 8 <= len; i++) {
        const IOHIDKeyboardEventData *base = (const IOHIDKeyboardEventData *)(buf + off);
        uint32_t size = base->size;
        if (size < 16 || off + size > len)
            break;
        switch (base->type) {
        case kIOHIDEventTypeKeyboard:
            printf("hid_event_smoketest: [%d] key page 0x%x usage 0x%02x %s%s\n", svc,
                   base->usagePage, base->usage, base->down ? "down" : "up",
                   base->options.repeat ? " (repeat)" : "");
            keys++;
            break;
        case kIOHIDEventTypePointer: {
            const IOHIDPointerEventData *p = (const IOHIDPointerEventData *)base;
            printf("hid_event_smoketest: [%d] pointer dx %.2f dy %.2f buttons 0x%x\n", svc,
                   fixed(p->position.x), fixed(p->position.y), p->button.mask);
            break;
        }
        case kIOHIDEventTypeButton: {
            const IOHIDButtonEventData *b = (const IOHIDButtonEventData *)base;
            printf("hid_event_smoketest: [%d] button mask 0x%x number %u state %u\n", svc,
                   b->mask, b->number, b->state);
            break;
        }
        case kIOHIDEventTypeScroll: {
            const IOHIDScrollEventData *s = (const IOHIDScrollEventData *)base;
            printf("hid_event_smoketest: [%d] scroll x %.2f y %.2f\n", svc,
                   fixed(s->position.x), fixed(s->position.y));
            break;
        }
        default:
            printf("hid_event_smoketest: [%d] event type %u (%u bytes)\n", svc, base->type, size);
        }
        off += size;
    }
    return keys;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    Service svc[kMaxServices];
    int n = 0, opened = 0;
    io_iterator_t it = IO_OBJECT_NULL;
    mach_port_t set = MACH_PORT_NULL;

    /* The user client reads the entitlement from this process's code
     * signature, and only if CS_SIGNED is set (xnu kern_cs.c:1463-1469). */
    uint32_t csflags = 0;
    int csr = csops(getpid(), CS_OPS_STATUS, &csflags, sizeof(csflags));
    uint8_t ent[1024];
    memset(ent, 0, sizeof(ent));
    int cse = csops(getpid(), CS_OPS_ENTITLEMENTS_BLOB, ent, sizeof(ent));
    printf("hid_event_smoketest: csops status %d flags 0x%08x (%s%s), entitlements blob %d (%u bytes)\n",
           csr, csflags, (csflags & CS_SIGNED) ? "signed" : "UNSIGNED",
           (csflags & CS_VALID) ? ", valid" : "", cse,
           cse == 0 ? (unsigned)((ent[4] << 24) | (ent[5] << 16) | (ent[6] << 8) | ent[7]) : 0u);

    if (IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOHIDEventService"), &it) != KERN_SUCCESS) {
        printf("hid_event_smoketest: FAIL (no IOHIDEventService matching)\n");
        return 1;
    }
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &set) != KERN_SUCCESS) {
        printf("hid_event_smoketest: FAIL (no port set)\n");
        return 1;
    }

    io_service_t s;
    while (n < kMaxServices && (s = IOIteratorNext(it)) != IO_OBJECT_NULL) {
        io_name_t cls = "";
        IOObjectGetClass(s, cls);
        Service *e = &svc[n];
        memset(e, 0, sizeof(*e));
        e->page = intProp(s, "PrimaryUsagePage");
        e->usage = intProp(s, "PrimaryUsage");
        kern_return_t kr = IOServiceOpen(s, mach_task_self(), kIOHIDEventServiceUserClientType, &e->connect);
        printf("hid_event_smoketest: [%d] %s PrimaryUsagePage 0x%x PrimaryUsage 0x%x: IOServiceOpen 0x%x\n",
               n, cls, e->page, e->usage, kr);
        IOObjectRelease(s);
        if (kr != KERN_SUCCESS) {
            n++;
            continue;
        }
        uint64_t options = 0;
        kr = IOConnectCallScalarMethod(e->connect, kIOHIDEventServiceUserClientOpen, &options, 1, NULL, NULL);
        mach_vm_address_t addr = 0;
        mach_vm_size_t size = 0;
        kern_return_t mkr = kr ? kr : IOConnectMapMemory64(e->connect, 0, mach_task_self(), &addr, &size, kIOMapAnywhere);
        e->port = IODataQueueAllocateNotificationPort();
        kern_return_t pkr = mkr ? mkr : IOConnectSetNotificationPort(e->connect, 0, e->port, 0);
        printf("hid_event_smoketest: [%d]   open 0x%x, map 0x%x (size 0x%llx), notify 0x%x\n",
               n, kr, mkr, (unsigned long long)size, pkr);
        if (!kr && !mkr && !pkr) {
            e->queue = (IODataQueueMemory *)(uintptr_t)addr;
            mach_port_insert_member(mach_task_self(), e->port, set);
            opened++;
        }
        n++;
    }
    IOObjectRelease(it);
    printf("hid_event_smoketest: %d service(s), %d open; listening %d s\n", n, opened, seconds);
    fflush(stdout);
    if (opened == 0) {
        printf("hid_event_smoketest: FAIL (no service opened)\n");
        return 1;
    }

    /* The queue is at most kQueueSizeMax (16 KiB,
     * IOHIDEventServiceUserClient.cpp:37), so no entry is bigger. An entry
     * that still does not fit is skipped, not left at the head, where it
     * would wedge the queue (IODataQueueClient.c returns kIOReturnNoSpace
     * without moving head). */
    static uint8_t buf[16384];
    int events = 0;
    /* Monotonic deadline: the wall clock can be stepped (fake-hwclock,
     * chronyd) while this runs. */
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    uint64_t ms_to_abs_num = (uint64_t)tb.denom * 1000000ull, ms_to_abs_den = tb.numer;
    uint64_t deadline = mach_absolute_time() + (uint64_t)seconds * 1000ull * ms_to_abs_num / ms_to_abs_den;
    for (;;) {
        uint64_t now = mach_absolute_time();
        if (now >= deadline)
            break;
        uint64_t left_ms = (deadline - now) * ms_to_abs_den / ms_to_abs_num + 1;
        struct { mach_msg_header_t h; mach_msg_trailer_t t; } msg;
        kern_return_t kr = mach_msg(&msg.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(msg), set,
                                    (mach_msg_timeout_t)left_ms, MACH_PORT_NULL);
        if (kr == MACH_RCV_TIMED_OUT)
            break;
        if (kr != KERN_SUCCESS && kr != MACH_RCV_TOO_LARGE) {
            printf("hid_event_smoketest: mach_msg failed 0x%x\n", kr);
            break;
        }
        for (int i = 0; i < n; i++) {
            if (!svc[i].queue)
                continue;
            uint32_t len = sizeof(buf);
            IOReturn r;
            while ((r = IODataQueueDequeue(svc[i].queue, buf, &len)) != kIOReturnUnderrun) {
                if (r == kIOReturnSuccess) {
                    decode(buf, len, i);
                    events++;
                } else {
                    printf("hid_event_smoketest: [%d] skipping an entry (0x%x)\n", i, r);
                    uint32_t skip = 0;
                    if (IODataQueueDequeue(svc[i].queue, NULL, &skip) != kIOReturnSuccess)
                        break;
                }
                len = sizeof(buf);
            }
        }
        fflush(stdout);
    }
    printf("hid_event_smoketest: %d event(s) received\n", events);
    printf("hid_event_smoketest: %s\n", events > 0 ? "PASS" : "NO EVENTS");
    return events > 0 ? 0 : 1;
}
