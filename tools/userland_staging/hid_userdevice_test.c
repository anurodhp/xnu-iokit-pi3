/*
 * hid_userdevice_test: create a userland HID keyboard and mouse
 * (IOHIDUserDevice) and type and move with them -- step H0 of
 * docs/bluetooth-stack-plan.md. The Bluetooth HID host (S5) will create one
 * such device per connected keyboard or mouse, so this proves the path
 * before any Bluetooth code exists:
 *
 *   IOHIDResource (personality in BCM2837Personalities.cpp)
 *     -> IOHIDResourceDeviceUserClient, type kIOHIDResourceUserClientTypeDevice
 *        (xnu iokit/IOKit/hid/IOHIDResourceUserClient.h:34-36)
 *     -> method Create with the serialized property dictionary
 *        (.h:50; IOHIDResourceUserClient.cpp, options 0 = register at once,
 *        :519) -> IOHIDUserDevice, an IOHIDDevice
 *     -> IOHIDInterface -> IOHIDEventDriver (the keyboard and pointer
 *        personalities) -> IOHIDEventService, which X's iohid driver and
 *        hid_event_smoketest read
 *     -> method HandleReport, scalar timestamp (0 = now) and the report
 *        bytes (handleReport, IOHIDResourceUserClient.cpp)
 *
 * Calls are IOKitUser's own IOHIDUserDevice.c sequence
 * (hid.subproj/IOHIDUserDevice.c:383-386, :533-536, :1073) without its
 * run-loop machinery. The user client needs the
 * com.apple.hid.manager.user-access-device entitlement
 * (IOHIDResourceUserClient.cpp:101-131), which build_hid_userdevice_test.sh
 * signs in.
 *
 * Usage: hid_userdevice_test [seconds]   (default 30)
 * After 5 s (for matching) it types "hi" and moves the pointer right and
 * back once a second until the time is up, then exits; the devices go
 * away with the process.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFSerialize.h>
#include <CoreFoundation/CoreFoundation.h>

/* IOHIDResourceUserClient.h:34-57 */
enum { kResourceTypeDevice = 0 };
enum { kMethodCreate = 0, kMethodTerminate = 1, kMethodHandleReport = 2 };

/* Boot keyboard: modifiers, reserved, six key codes (HID 1.11 appendix B.1). */
static const uint8_t kKeyboardDesc[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,             /* GD, Keyboard, Application */
	0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             /* 8 modifier bits */
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,             /* reserved byte */
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
	0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, /* 6 key codes */
	0xc0,
};
/* Boot mouse: three buttons, relative X and Y (HID 1.11 appendix B.2). */
static const uint8_t kMouseDesc[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01,             /* GD, Mouse, Application */
	0x09, 0x01, 0xa1, 0x00,                         /* Pointer, Physical */
	0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
	0x95, 0x03, 0x75, 0x01, 0x81, 0x02,             /* 3 buttons */
	0x95, 0x01, 0x75, 0x05, 0x81, 0x01,             /* padding */
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7f,
	0x75, 0x08, 0x95, 0x02, 0x81, 0x06,             /* X, Y relative */
	0xc0, 0xc0,
};

static void
setNum(CFMutableDictionaryRef d, const char *key, int v)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &v);
	CFDictionarySetValue(d, k, n);
	CFRelease(k);
	CFRelease(n);
}

static void
setStr(CFMutableDictionaryRef d, const char *key, const char *v)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	CFStringRef s = CFStringCreateWithCString(NULL, v, kCFStringEncodingUTF8);
	CFDictionarySetValue(d, k, s);
	CFRelease(k);
	CFRelease(s);
}

static io_connect_t
createDevice(io_service_t resource, const char *name, int product, const uint8_t *desc, size_t descLen)
{
	CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDataRef d = CFDataCreate(NULL, desc, (CFIndex)descLen);
	CFDictionarySetValue(props, CFSTR("ReportDescriptor"), d);
	CFRelease(d);
	setNum(props, "VendorID", 0x05ac);
	setNum(props, "ProductID", product);
	setStr(props, "Product", name);
	setStr(props, "Manufacturer", "hid_userdevice_test");
	setStr(props, "Transport", "Virtual");

	CFDataRef ser = IOCFSerialize(props, 0);
	CFRelease(props);
	if (ser == NULL) {
		fprintf(stderr, "hid_userdevice_test: IOCFSerialize failed\n");
		return MACH_PORT_NULL;
	}

	io_connect_t conn = MACH_PORT_NULL;
	kern_return_t kr = IOServiceOpen(resource, mach_task_self(), kResourceTypeDevice, &conn);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "hid_userdevice_test: IOServiceOpen(IOHIDResource): 0x%x%s\n", kr,
		    kr == kIOReturnNotPermitted || kr == kIOReturnNotPrivileged ? " (entitlement?)" : "");
		CFRelease(ser);
		return MACH_PORT_NULL;
	}
	uint64_t options = 0;
	kr = IOConnectCallMethod(conn, kMethodCreate, &options, 1,
	    CFDataGetBytePtr(ser), (size_t)CFDataGetLength(ser), NULL, NULL, NULL, NULL);
	CFRelease(ser);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "hid_userdevice_test: Create(%s): 0x%x\n", name, kr);
		IOServiceClose(conn);
		return MACH_PORT_NULL;
	}
	io_service_t dev = MACH_PORT_NULL;
	uint64_t id = 0;
	if (IOConnectGetService(conn, &dev) == KERN_SUCCESS) {
		IORegistryEntryGetRegistryEntryID(dev, &id);
		IOObjectRelease(dev);
	}
	printf("hid_userdevice_test: created %s (registry id 0x%llx)\n", name, (unsigned long long)id);
	return conn;
}

static int
report(io_connect_t conn, const uint8_t *r, size_t len)
{
	uint64_t timestamp = 0;   /* 0: the kernel stamps it (handleReport) */
	kern_return_t kr = IOConnectCallMethod(conn, kMethodHandleReport, &timestamp, 1, r, len,
	    NULL, NULL, NULL, NULL);
	if (kr != KERN_SUCCESS)
		fprintf(stderr, "hid_userdevice_test: HandleReport: 0x%x\n", kr);
	return kr == KERN_SUCCESS ? 0 : -1;
}

int
main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 30;
	setvbuf(stdout, NULL, _IOLBF, 0);   /* the QEMU test reads a redirected log */
	if (secs <= 0)
		secs = 30;

	io_service_t resource = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOHIDResource"));
	if (resource == MACH_PORT_NULL) {
		fprintf(stderr, "hid_userdevice_test: no IOHIDResource service (personality missing?)\n");
		return 1;
	}
	io_connect_t kbd = createDevice(resource, "Virtual Keyboard", 0x8001, kKeyboardDesc, sizeof(kKeyboardDesc));
	io_connect_t mouse = createDevice(resource, "Virtual Mouse", 0x8002, kMouseDesc, sizeof(kMouseDesc));
	IOObjectRelease(resource);
	if (kbd == MACH_PORT_NULL || mouse == MACH_PORT_NULL)
		return 1;

	sleep(5);
	int fails = 0, sent = 0;
	for (int t = 5; t < secs; t++) {
		static const uint8_t keys[] = { 0x0b, 0x0c };   /* h, i (usage page 7) */
		for (size_t i = 0; i < sizeof(keys); i++) {
			uint8_t down[8] = { 0, 0, keys[i] }, up[8] = { 0 };
			fails += report(kbd, down, sizeof(down));
			fails += report(kbd, up, sizeof(up));
		}
		uint8_t right[3] = { 0, 20, 0 }, left[3] = { 0, (uint8_t)-20, 0 };
		fails += report(mouse, right, sizeof(right));
		fails += report(mouse, left, sizeof(left));
		sent += 6;
		sleep(1);
	}
	printf("hid_userdevice_test: sent %d reports, %d failed\n", sent, -fails);
	IOConnectCallMethod(kbd, kMethodTerminate, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
	IOConnectCallMethod(mouse, kMethodTerminate, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
	IOServiceClose(kbd);
	IOServiceClose(mouse);
	return fails ? 1 : 0;
}
