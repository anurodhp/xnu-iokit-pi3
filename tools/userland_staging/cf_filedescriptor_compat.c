/* Real, from-scratch CFFileDescriptor implementation -- DAR-252.
 *
 * WHY THIS FILE EXISTS AT ALL (read before touching): CFFileDescriptor.c
 * has never been open-sourced by Apple, on any release. Confirmed twice,
 * independently: (1) apple-oss-distributions/CF's real `main` branch
 * tree (`gh api repos/apple-oss-distributions/CF/git/trees/main`) has
 * no CFFileDescriptor.c/.h at all -- it has CFSocket.c/.h and
 * CFConcreteStreams.c (which merely *calls* the public
 * CFFileDescriptor* entry points, it doesn't define them); (2) this
 * project's pinned swift-corelibs-foundation fork (the real CF this
 * project builds against) also has no such file. The real PUBLIC
 * header (CFFileDescriptor.h) *is* shipped in every real Apple SDK --
 * this project's pinned Xcode-12 iPhoneOS SDK has a real, unmodified
 * copy at .../CoreFoundation.framework/Headers/CFFileDescriptor.h,
 * confirmed present and used unmodified below (no shadow/weak-import
 * compat header needed anymore -- see build_configd.sh's DAR-252
 * comment and configd_cffiledescriptor_weak_compat.h for the OLD
 * approach this file replaces). So the real public *contract* is
 * known exactly; only the real *implementation* was never released.
 * This is the "last resort custom" case CLAUDE.md's source-priority
 * rule carves out explicitly (same class as this project's from-scratch
 * SHA-256/384/512), not a shortcut taken before checking.
 *
 * real caller: third_party/bootp/IPConfiguration.bproj/FDSet.c's
 * FDCalloutCreateFileDescriptor() (the raw-BPF-fd DHCP/ARP receive
 * path FDCalloutCreate() takes for a non-socket fd -- see
 * arp_client_open_fd(), arp_session.c:831). Confirmed via that real,
 * pinned source (not guessed) that it only ever uses:
 *   CFFileDescriptorCreate, CFFileDescriptorEnableCallBacks (with
 *   ONLY kCFFileDescriptorReadCallBack), CFFileDescriptorCreateRunLoopSource,
 *   CFFileDescriptorGetNativeDescriptor, CFFileDescriptorInvalidate.
 * It also confirms real one-shot-then-manual-rearm semantics: its own
 * read callback (FDCalloutFileDescriptorReceive) re-calls
 * CFFileDescriptorEnableCallBacks(kCFFileDescriptorReadCallBack) itself
 * every time after handling one read -- i.e. this project's
 * implementation does not need to auto-reenable callbacks the way
 * CFSocket.c optionally does (kCFSocketAutomaticallyReenableReadCallBack);
 * a fired callback type simply stays disabled until the client
 * re-enables it, matching CFFileDescriptor.h's own doc comment. This
 * genuinely narrows the real scope needed here versus CFSocket.c's much
 * larger surface (no accept/connect/data-queue/address handling).
 *
 * MECHANISM: modeled directly on this exact codebase's own real,
 * already-built, already-QEMU-verified CFSocket.c (same RunLoop.subproj,
 * same __CFPort=mach_port_t Darwin build) rather than invented from
 * nothing -- CFSocket.c's own real "SocketManager" background thread
 * (see its own comment: "separate SocketManager thread who uses
 * select() to watch the sockets' fds") is precedent this codebase
 * already trusts for exactly this fd-readiness-to-RunLoopSource-signal
 * problem. This file is a much narrower analog: one shared background
 * thread does select() over every live CFFileDescriptor's native fd
 * (gated by which of Read/Write are currently enabled) and, on
 * readiness, disables that callback type (one-shot), signals the
 * descriptor's real v0 CFRunLoopSource (CFRunLoopSourceSignal), and
 * wakes every runloop it's scheduled on (CFRunLoopWakeUp) -- the exact
 * same v0-source signal/wake pattern CFSocket.c's own
 * __CFSocketPerformV0/__CFSocketManager use, confirmed by direct
 * reading of that file (schedule/cancel/perform triple wired into
 * CFRunLoopSourceContext version 0, see CFSocketCreateRunLoopSource).
 * A dedicated self-pipe (not CFSocket's real socketpair -- a plain
 * pipe(2) is simpler and equally real/standard for this) interrupts an
 * in-progress select() whenever the watched-fd set or a fd's enabled
 * bits change, so EnableCallBacks doesn't wait for an unrelated fd to
 * become ready first.
 *
 * LOCK ORDERING (real hazard found and fixed during design, not
 * shipped blind): CFSocket.c's own header documents its lock order as
 * (1) all-sockets lock, (2) one socket's own lock, (3) active-sockets
 * lock -- i.e. the manager-thread-visible global lock is innermost
 * there. This file instead NEVER acquires the global active-descriptor
 * lock (__CFFDActiveLock) while holding a CFFileDescriptor's own lock
 * -- every call into __CFFDManagerAdd/__CFFDManagerRemove/
 * __CFFDManagerWake happens strictly AFTER releasing the descriptor's
 * own lock. Reason: the manager thread's scan loop necessarily acquires
 * __CFFDActiveLock BEFORE each descriptor's own lock (it has to find
 * the descriptor via the global array first) -- if any public API call
 * acquired the descriptor lock first and then tried to take
 * __CFFDActiveLock (the naive translation of CFSocket's own
 * Schedule/Cancel, which call straight into socket-manager bookkeeping
 * while still holding the socket's own lock), that's a textbook AB-BA
 * deadlock the moment the manager thread's scan happens to reach that
 * exact descriptor concurrently. Every function below computes what it
 * needs to do while the descriptor lock is held, unlocks, THEN talks to
 * the manager -- global lock is always acquired outermost, full stop.
 *
 * Only real API surface implemented (matches the real, unmodified SDK
 * header exactly -- CFFileDescriptorGetTypeID/Create/
 * GetNativeDescriptor/GetContext/EnableCallBacks/DisableCallBacks/
 * Invalidate/IsValid/CreateRunLoopSource, all 9 real entry points, not
 * just the 5 build_configd.sh's old -Wl,-U list happened to need).
 */

#include <CoreFoundation/CFFileDescriptor.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFRunLoop.h>
#include "CFInternal.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

struct __CFFileDescriptor {
    CFRuntimeBase _base;
    CFLock_t _lock;
    CFFileDescriptorNativeDescriptor _fd;      /* -1 once invalidated */
    Boolean _closeOnInvalidate;
    Boolean _valid;
    CFFileDescriptorCallBack _callout;         /* immutable */
    CFFileDescriptorContext _context;          /* immutable */
    CFMutableArrayRef _runLoops;               /* retaining, matches CFSocket.c's own _runLoops */
    CFRunLoopSourceRef _source0;
    CFOptionFlags _enabledCallBacks;           /* client-armed, one-shot per real FDSet.c usage */
    CFOptionFlags _signalledCallBacks;          /* fired, awaiting delivery on the perform callback */
    SInt32 _scheduleCount;                     /* refcount across runloops/modes, mirrors CFSocket's _socketSetCount */
};

CF_INLINE void __CFFDLock(CFFileDescriptorRef f) { __CFLock(&f->_lock); }
CF_INLINE void __CFFDUnlock(CFFileDescriptorRef f) { __CFUnlock(&f->_lock); }

/* ---- global manager-thread bookkeeping ---- */

static CFLock_t __CFFDActiveLock = CFLockInit;
static CFMutableArrayRef __CFFDActiveDescriptors = NULL;   /* retaining */
static int __CFFDWakeupPipe[2] = {-1, -1};
static Boolean __CFFDManagerStarted = false;

static void *__CFFDManager(void *arg);

/* Must be called with __CFFDActiveLock held. */
static void __CFFDManagerStartIfNeeded(void) {
    if (__CFFDManagerStarted) return;
    if (NULL == __CFFDActiveDescriptors) {
        __CFFDActiveDescriptors = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
    }
    if (0 != pipe(__CFFDWakeupPipe)) {
        /* Real, extremely-unlikely failure (fd exhaustion). Leave the
         * manager unstarted rather than crashing -- callers just never
         * see a callback fire, an honest degraded mode, not a corrupt
         * one. */
        return;
    }
    int flags = fcntl(__CFFDWakeupPipe[0], F_GETFL, 0);
    fcntl(__CFFDWakeupPipe[0], F_SETFL, flags | O_NONBLOCK);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    if (0 == pthread_create(&tid, &attr, __CFFDManager, NULL)) {
        __CFFDManagerStarted = true;
    }
    pthread_attr_destroy(&attr);
}

static void __CFFDManagerWake(void) {
    __CFLock(&__CFFDActiveLock);
    if (__CFFDWakeupPipe[1] >= 0) {
        uint8_t b = 0;
        ssize_t r = write(__CFFDWakeupPipe[1], &b, 1);
        (void)r; /* best-effort -- pipe is only ever drained empty by the manager, never fills under real use */
    }
    __CFUnlock(&__CFFDActiveLock);
}

/* Caller must NOT hold f's own lock (see file header's lock-ordering note). */
static void __CFFDManagerAdd(CFFileDescriptorRef f) {
    __CFLock(&__CFFDActiveLock);
    __CFFDManagerStartIfNeeded();
    if (__CFFDActiveDescriptors) {
        CFIndex count = CFArrayGetCount(__CFFDActiveDescriptors);
        if (!CFArrayContainsValue(__CFFDActiveDescriptors, CFRangeMake(0, count), f)) {
            CFArrayAppendValue(__CFFDActiveDescriptors, f);
        }
        if (__CFFDWakeupPipe[1] >= 0) {
            uint8_t b = 0;
            ssize_t r = write(__CFFDWakeupPipe[1], &b, 1);
            (void)r;
        }
    }
    __CFUnlock(&__CFFDActiveLock);
}

/* Caller must NOT hold f's own lock. Safe to call even if f was never added. */
static void __CFFDManagerRemove(CFFileDescriptorRef f) {
    __CFLock(&__CFFDActiveLock);
    if (__CFFDActiveDescriptors) {
        CFIndex count = CFArrayGetCount(__CFFDActiveDescriptors);
        CFIndex idx = CFArrayGetFirstIndexOfValue(__CFFDActiveDescriptors, CFRangeMake(0, count), f);
        if (idx >= 0) CFArrayRemoveValueAtIndex(__CFFDActiveDescriptors, idx);
        if (__CFFDWakeupPipe[1] >= 0) {
            uint8_t b = 0;
            ssize_t r = write(__CFFDWakeupPipe[1], &b, 1);
            (void)r;
        }
    }
    __CFUnlock(&__CFFDActiveLock);
}

static void *__CFFDManager(void *arg) {
    (void)arg;
#if !TARGET_OS_LINUX
    pthread_setname_np("com.apple.CFFileDescriptor.private");
#endif
    for (;;) {
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        int maxfd = -1;

        __CFLock(&__CFFDActiveLock);
        int wakeupReadFd = __CFFDWakeupPipe[0];
        if (wakeupReadFd >= 0) {
            FD_SET(wakeupReadFd, &readfds);
            if (wakeupReadFd > maxfd) maxfd = wakeupReadFd;
        }
        CFIndex count = __CFFDActiveDescriptors ? CFArrayGetCount(__CFFDActiveDescriptors) : 0;
        for (CFIndex i = 0; i < count; i++) {
            CFFileDescriptorRef f = (CFFileDescriptorRef)CFArrayGetValueAtIndex(__CFFDActiveDescriptors, i);
            __CFFDLock(f);
            if (f->_valid && f->_fd >= 0) {
                if (f->_enabledCallBacks & kCFFileDescriptorReadCallBack) {
                    FD_SET(f->_fd, &readfds);
                    if (f->_fd > maxfd) maxfd = f->_fd;
                }
                if (f->_enabledCallBacks & kCFFileDescriptorWriteCallBack) {
                    FD_SET(f->_fd, &writefds);
                    if (f->_fd > maxfd) maxfd = f->_fd;
                }
            }
            __CFFDUnlock(f);
        }
        __CFUnlock(&__CFFDActiveLock);

        int nr = select(maxfd + 1, &readfds, &writefds, NULL, NULL);
        if (nr < 0) {
            if (EINTR == errno) continue;
            usleep(10000); /* avoid a hot spin loop on a real, unexpected select() failure */
            continue;
        }

        if (wakeupReadFd >= 0 && FD_ISSET(wakeupReadFd, &readfds)) {
            uint8_t buf[64];
            while (read(wakeupReadFd, buf, sizeof(buf)) > 0) { }
        }

        __CFLock(&__CFFDActiveLock);
        CFArrayRef snapshot = __CFFDActiveDescriptors ? CFArrayCreateCopy(kCFAllocatorSystemDefault, __CFFDActiveDescriptors) : NULL;
        __CFUnlock(&__CFFDActiveLock);
        if (NULL == snapshot) continue;

        CFIndex n = CFArrayGetCount(snapshot);
        for (CFIndex i = 0; i < n; i++) {
            CFFileDescriptorRef f = (CFFileDescriptorRef)CFArrayGetValueAtIndex(snapshot, i);
            __CFFDLock(f);
            Boolean fire = false;
            if (f->_valid && f->_fd >= 0) {
                if ((f->_enabledCallBacks & kCFFileDescriptorReadCallBack) && FD_ISSET(f->_fd, &readfds)) {
                    f->_enabledCallBacks &= ~(CFOptionFlags)kCFFileDescriptorReadCallBack;
                    f->_signalledCallBacks |= kCFFileDescriptorReadCallBack;
                    fire = true;
                }
                if ((f->_enabledCallBacks & kCFFileDescriptorWriteCallBack) && FD_ISSET(f->_fd, &writefds)) {
                    f->_enabledCallBacks &= ~(CFOptionFlags)kCFFileDescriptorWriteCallBack;
                    f->_signalledCallBacks |= kCFFileDescriptorWriteCallBack;
                    fire = true;
                }
            }
            CFRunLoopSourceRef source0 = NULL;
            CFArrayRef runLoopsCopy = NULL;
            if (fire && f->_source0 && CFRunLoopSourceIsValid(f->_source0)) {
                source0 = (CFRunLoopSourceRef)CFRetain(f->_source0);
                if (f->_runLoops) runLoopsCopy = (CFArrayRef)CFRetain(f->_runLoops);
            }
            __CFFDUnlock(f);

            if (source0) {
                CFRunLoopSourceSignal(source0);
                if (runLoopsCopy) {
                    CFIndex rc = CFArrayGetCount(runLoopsCopy);
                    for (CFIndex j = 0; j < rc; j++) {
                        CFRunLoopWakeUp((CFRunLoopRef)CFArrayGetValueAtIndex(runLoopsCopy, j));
                    }
                    CFRelease(runLoopsCopy);
                }
                CFRelease(source0);
            }
        }
        CFRelease(snapshot);
    }
    return NULL;
}

/* ---- CFRuntime plumbing ---- */

static void __CFFileDescriptorDeallocate(CFTypeRef cf) {
    CFFileDescriptorRef f = (CFFileDescriptorRef)cf;
    /* Real callers only ever reach dealloc after invalidate (matches
     * CFSocket.c's own "can only get here sometime after being
     * invalidated" comment) -- defensively invalidate anyway rather
     * than assume. */
    CFFileDescriptorInvalidate(f);
    if (f->_runLoops) {
        CFRelease(f->_runLoops);
        f->_runLoops = NULL;
    }
}

static CFStringRef __CFFileDescriptorCopyDescription(CFTypeRef cf) {
    CFFileDescriptorRef f = (CFFileDescriptorRef)cf;
    return CFStringCreateWithFormat(CFGetAllocator(f), NULL,
        CFSTR("<CFFileDescriptor %p [%p]>{valid = %s, fd = %d}"),
        cf, CFGetAllocator(f), f->_valid ? "Yes" : "No", (int)f->_fd);
}

static const CFRuntimeClass __CFFileDescriptorClass = {
    0,
    "CFFileDescriptor",
    NULL,       // init
    NULL,       // copy
    __CFFileDescriptorDeallocate,
    NULL,       // equal
    NULL,       // hash
    NULL,       // copyFormattingDesc
    __CFFileDescriptorCopyDescription,
};

CFTypeID CFFileDescriptorGetTypeID(void) {
    static CFTypeID typeID = _kCFRuntimeNotATypeID;
    static dispatch_once_t initOnce;
    dispatch_once(&initOnce, ^{
        typeID = _CFRuntimeRegisterClass(&__CFFileDescriptorClass);
    });
    return typeID;
}

/* ---- public API ---- */

CFFileDescriptorRef CFFileDescriptorCreate(CFAllocatorRef allocator, CFFileDescriptorNativeDescriptor fd, Boolean closeOnInvalidate, CFFileDescriptorCallBack callout, const CFFileDescriptorContext *context) {
    CHECK_FOR_FORK();
    CFFileDescriptorRef f = (CFFileDescriptorRef)_CFRuntimeCreateInstance(allocator, CFFileDescriptorGetTypeID(), sizeof(struct __CFFileDescriptor) - sizeof(CFRuntimeBase), NULL);
    if (NULL == f) return NULL;
    f->_lock = CFLockInit;
    f->_fd = fd;
    f->_closeOnInvalidate = closeOnInvalidate;
    f->_valid = true;
    f->_callout = callout;
    memset(&f->_context, 0, sizeof(f->_context));
    if (context) {
        memcpy(&f->_context, context, sizeof(CFFileDescriptorContext));
        if (context->retain) {
            f->_context.info = (void *)context->retain(context->info);
        }
    }
    CFArrayCallBacks retainingCallbacks = kCFTypeArrayCallBacks;
    retainingCallbacks.copyDescription = NULL;
    f->_runLoops = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &retainingCallbacks);
    f->_source0 = NULL;
    f->_enabledCallBacks = 0;
    f->_signalledCallBacks = 0;
    f->_scheduleCount = 0;
    return f;
}

CFFileDescriptorNativeDescriptor CFFileDescriptorGetNativeDescriptor(CFFileDescriptorRef f) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    return f->_fd;
}

void CFFileDescriptorGetContext(CFFileDescriptorRef f, CFFileDescriptorContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    *context = f->_context;
}

Boolean CFFileDescriptorIsValid(CFFileDescriptorRef f) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    __CFFDLock(f);
    Boolean valid = f->_valid;
    __CFFDUnlock(f);
    return valid;
}

void CFFileDescriptorEnableCallBacks(CFFileDescriptorRef f, CFOptionFlags callBackTypes) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    Boolean valid;
    __CFFDLock(f);
    valid = f->_valid;
    if (valid) {
        f->_enabledCallBacks |= (callBackTypes & (kCFFileDescriptorReadCallBack | kCFFileDescriptorWriteCallBack));
    }
    __CFFDUnlock(f);
    if (valid) __CFFDManagerWake();
}

void CFFileDescriptorDisableCallBacks(CFFileDescriptorRef f, CFOptionFlags callBackTypes) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    __CFFDLock(f);
    f->_enabledCallBacks &= ~(callBackTypes & (kCFFileDescriptorReadCallBack | kCFFileDescriptorWriteCallBack));
    f->_signalledCallBacks &= ~(callBackTypes & (kCFFileDescriptorReadCallBack | kCFFileDescriptorWriteCallBack));
    __CFFDUnlock(f);
}

void CFFileDescriptorInvalidate(CFFileDescriptorRef f) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    __CFFDLock(f);
    if (!f->_valid) {
        __CFFDUnlock(f);
        return;
    }
    f->_valid = false;
    CFRunLoopSourceRef source0 = f->_source0;
    f->_source0 = NULL;
    CFFileDescriptorNativeDescriptor fd = f->_fd;
    Boolean doClose = f->_closeOnInvalidate;
    f->_fd = -1;
    void *contextInfo = f->_context.info;
    void (*contextRelease)(void *) = f->_context.release;
    f->_context.info = NULL;
    __CFFDUnlock(f);

    __CFFDManagerRemove(f);   /* f-lock NOT held -- see file header */

    if (source0) {
        CFRunLoopSourceInvalidate(source0);
        CFRelease(source0);
    }
    if (doClose && fd >= 0) close(fd);
    if (contextInfo && contextRelease) contextRelease(contextInfo);
}

static void __CFFileDescriptorSchedule(void *info, CFRunLoopRef rl, CFStringRef mode) {
    (void)mode;
    CFFileDescriptorRef f = (CFFileDescriptorRef)info;
    Boolean needAdd = false;
    __CFFDLock(f);
    if (f->_valid) {
        CFArrayAppendValue(f->_runLoops, rl);
        f->_scheduleCount++;
        needAdd = (1 == f->_scheduleCount);
    }
    __CFFDUnlock(f);
    if (needAdd) __CFFDManagerAdd(f);   /* f-lock NOT held -- see file header */
}

static void __CFFileDescriptorCancel(void *info, CFRunLoopRef rl, CFStringRef mode) {
    (void)mode;
    CFFileDescriptorRef f = (CFFileDescriptorRef)info;
    Boolean needRemove = false;
    __CFFDLock(f);
    if (f->_scheduleCount > 0) f->_scheduleCount--;
    needRemove = (0 == f->_scheduleCount);
    if (f->_runLoops) {
        CFIndex count = CFArrayGetCount(f->_runLoops);
        CFIndex idx = CFArrayGetFirstIndexOfValue(f->_runLoops, CFRangeMake(0, count), rl);
        if (idx >= 0) CFArrayRemoveValueAtIndex(f->_runLoops, idx);
    }
    __CFFDUnlock(f);
    if (needRemove) __CFFDManagerRemove(f);   /* f-lock NOT held -- see file header */
}

static void __CFFileDescriptorPerformV0(void *info) {
    CFFileDescriptorRef f = (CFFileDescriptorRef)info;
    __CFFDLock(f);
    if (!f->_valid) {
        __CFFDUnlock(f);
        return;
    }
    CFOptionFlags fired = f->_signalledCallBacks;
    f->_signalledCallBacks = 0;
    CFFileDescriptorCallBack callout = f->_callout;
    Boolean willCall = (0 != fired) && (NULL != callout);
    void *contextInfo = f->_context.info;
    void (*contextRelease)(void *) = f->_context.release;
    /* Retain WHILE STILL HOLDING f's lock -- matches CFSocket.c's own
     * real __CFSocketPerformV0 (it calls s->_context.retain(contextInfo)
     * before __CFSocketDoCallback's internal unlock, not after). Doing
     * this after unlocking would race a concurrent
     * CFFileDescriptorInvalidate: Invalidate could read the same
     * contextInfo, call contextRelease on it, and free/tear it down
     * before this retain call ever ran -- a real use-after-free, not
     * just a theoretical one, since EnableCallBacks/Invalidate/the
     * manager thread's wake are all real concurrent callers of this
     * same descriptor from real different threads. Gated on willCall,
     * not just fired: retaining here but skipping the matching release
     * below (the old bug) when callout is NULL would leak one ref. */
    void *retainedInfo = (willCall && f->_context.retain) ? f->_context.retain(contextInfo) : contextInfo;
    __CFFDUnlock(f);

    if (!willCall) return;

    if (fired & kCFFileDescriptorReadCallBack) callout(f, kCFFileDescriptorReadCallBack, retainedInfo);
    if (fired & kCFFileDescriptorWriteCallBack) callout(f, kCFFileDescriptorWriteCallBack, retainedInfo);
    if (contextRelease) contextRelease(retainedInfo);
}

CFRunLoopSourceRef CFFileDescriptorCreateRunLoopSource(CFAllocatorRef allocator, CFFileDescriptorRef f, CFIndex order) {
    CHECK_FOR_FORK();
    CFRunLoopSourceRef result = NULL;
    __CFGenericValidateType(f, CFFileDescriptorGetTypeID());
    __CFFDLock(f);
    if (f->_valid) {
        if (f->_source0 && !CFRunLoopSourceIsValid(f->_source0)) {
            CFRelease(f->_source0);
            f->_source0 = NULL;
        }
        if (NULL == f->_source0) {
            CFRunLoopSourceContext context;
            memset(&context, 0, sizeof(context));
            context.version = 0;
            context.info = f;
            context.retain = CFRetain;
            context.release = CFRelease;
            context.copyDescription = CFCopyDescription;
            context.equal = CFEqual;
            context.hash = CFHash;
            context.schedule = __CFFileDescriptorSchedule;
            context.cancel = __CFFileDescriptorCancel;
            context.perform = __CFFileDescriptorPerformV0;
            f->_source0 = CFRunLoopSourceCreate(allocator, order, &context);
        }
        if (f->_source0) {
            CFRetain(f->_source0);
            result = f->_source0;
        }
    }
    __CFFDUnlock(f);
    return result;
}
