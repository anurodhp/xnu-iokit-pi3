/*
 * weak_proxy_smoketest (DAR-450): __weak references to GNUstep's NSProxy
 * on Apple's objc4.
 *
 * objc4 treats every root class other than NSObject as having custom
 * retain/release (runtime/objc-runtime-new.mm:865-867, "Custom Root
 * class"), so a weak store or load of an NSProxy instance, or of the class
 * object of any NSProxy subclass, asks the object itself -- with the
 * SideTable stripe lock held -- via -allowsWeakReference
 * (runtime/objc-weak.mm:403-417) and -retainWeakReference
 * (runtime/NSObject.mm:566-580). Both sites treat "not implemented" as a
 * lookup result equal to _objc_msgForward, but lookUpImpOrForward returns
 * _objc_msgForward_impcache (objc-runtime-new.mm:6395), so an unimplemented
 * selector is forwarded instead. GNUstep's forward handler then raised
 * "unrecognized selector" (+[GSListener allowsWeakReference], from
 * gnustep-gui's NSRegisterServicesProvider), the exception unwound through
 * storeWeak with the stripe lock still held, and the next weak operation on
 * that stripe aborted in _os_unfair_lock_recursive_abort -- Terminal.app
 * and Ink.app at -[NSApplication finishLaunching].
 *
 * Each case runs in its own child process (this binary re-executed with
 * the case name), so a leaked lock that aborts one case cannot hide the
 * others. After its weak operations every case takes the stripe lock of
 * each referent again, through a weak store to an NSObject allocated until
 * one lands on the same stripe (StripedMap::indexForPointer,
 * runtime/objc-private.h:925-928, 8 stripes on iOS targets, :913-914),
 * which aborts if the lock leaked. The last line is
 * "weak_proxy_smoketest: PASS" only if every case passed.
 */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

id objc_initWeak(id *location, id val);
id objc_storeWeak(id *location, id val);
id objc_loadWeakRetained(id *location);
void objc_destroyWeak(id *location);

static int failures;
#define CHECK(name, cond) do { \
        int ok_ = (cond) ? 1 : 0; \
        printf("%s %s\n", ok_ ? "ok  " : "FAIL", name); \
        fflush(stdout); \
        if (!ok_) failures++; \
    } while (0)

/* objc4 runtime/objc-private.h:925-928 with StripeCount 8 (:913-914). */
static unsigned stripe(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    return (unsigned)(((a >> 4) ^ (a >> 9)) % 8);
}

/* Takes the SideTable lock of p's stripe again, through a weak store to an
 * NSObject on the same stripe. Aborts (SIGTRAP) if the lock leaked. */
static void touchStripe(const void *p)
{
    NSMutableArray *keep = [NSMutableArray array];
    unsigned want = stripe(p);
    for (int i = 0; i < 4096; i++) {
        NSObject *o = [[NSObject alloc] init];
        [keep addObject: o];
        [o release];
        if (stripe(o) == want) {
            id w;
            objc_initWeak(&w, o);
            objc_destroyWeak(&w);
            return;
        }
    }
    printf("FAIL no NSObject landed on stripe %u\n", want);
    failures++;
}

/* Stands in for gnustep-gui's GSListener: an NSProxy subclass whose class
 * object is registered as a notification observer. */
@interface ClassObserverProxy : NSProxy
+ (void) note: (NSNotification *)n;
@end
static int classNotes;
@implementation ClassObserverProxy
+ (void) note: (NSNotification *)n { classNotes++; }
@end

@interface Target : NSObject
{
@public
    int notes;
}
- (void) note: (NSNotification *)n;
@end
@implementation Target
- (void) note: (NSNotification *)n { notes++; }
@end

/* An NSProxy that forwards everything it does not implement to target. */
@interface ForwardingProxy : NSProxy
{
    Target *target;
}
- (id) initWithTarget: (Target *)t;
@end
@implementation ForwardingProxy
- (id) initWithTarget: (Target *)t { target = [t retain]; return self; }
- (void) dealloc { [target release]; [super dealloc]; }
- (NSMethodSignature *) methodSignatureForSelector: (SEL)s
{
    return [target methodSignatureForSelector: s];
}
- (void) forwardInvocation: (NSInvocation *)inv { [inv invokeWithTarget: target]; }
@end

/* (b) A weak reference to the class object of an NSProxy subclass. */
static void caseProxyClass(void)
{
    id cls = [ClassObserverProxy class];
    id w = nil, r = nil;
    BOOL raised = NO;
    NS_DURING
        objc_initWeak(&w, cls);
        r = objc_loadWeakRetained(&w);
        objc_destroyWeak(&w);
    NS_HANDLER
        raised = YES;
        printf("     raised %s: %s\n", [[localException name] UTF8String],
               [[localException reason] UTF8String]);
    NS_ENDHANDLER
    CHECK("weak store/load of an NSProxy subclass's class object does not raise", !raised);
    CHECK("objc_loadWeakRetained returns the class object", r == cls);
    touchStripe(cls);
    CHECK("stripe lock of the class object is free afterwards", 1);
}

/* (b) A weak reference to an NSProxy instance. The weak-load retain must
 * land on the proxy itself, not be forwarded to its target. */
static void caseProxyInstance(void)
{
    Target *t = [[Target alloc] init];
    ForwardingProxy *p = [[ForwardingProxy alloc] initWithTarget: t];
    NSUInteger prc = [p retainCount], trc = [t retainCount];
    id w = nil, r = nil;
    BOOL raised = NO;
    NS_DURING
        objc_initWeak(&w, p);
        r = objc_loadWeakRetained(&w);
    NS_HANDLER
        raised = YES;
        printf("     raised %s: %s\n", [[localException name] UTF8String],
               [[localException reason] UTF8String]);
    NS_ENDHANDLER
    CHECK("weak store/load of an NSProxy instance does not raise", !raised);
    CHECK("objc_loadWeakRetained returns the proxy", r == p);
    CHECK("the weak-load retain is the proxy's, not its target's",
          [p retainCount] == prc + 1 && [t retainCount] == trc);
    if (r == p) [r release];
    [p release];
    CHECK("the weak reference is cleared when the proxy is deallocated",
          objc_loadWeakRetained(&w) == nil);
    objc_destroyWeak(&w);
    touchStripe(p);
    CHECK("stripe lock of the proxy is free afterwards", 1);
    [t release];
}

/* (b) NSProtocolChecker, a real gnustep-base NSProxy subclass. */
static void caseProtocolChecker(void)
{
    Target *t = [[Target alloc] init];
    id pc = [[NSProtocolChecker alloc] initWithTarget: t
                                          protocol: @protocol(NSObject)];
    id w = nil, r = nil;
    BOOL raised = NO;
    NS_DURING
        objc_initWeak(&w, pc);
        r = objc_loadWeakRetained(&w);
    NS_HANDLER
        raised = YES;
        printf("     raised %s: %s\n", [[localException name] UTF8String],
               [[localException reason] UTF8String]);
    NS_ENDHANDLER
    CHECK("weak store/load of an NSProtocolChecker does not raise", !raised);
    CHECK("objc_loadWeakRetained returns the checker", r == pc);
    if (r == pc) [r release];
    objc_destroyWeak(&w);
    touchStripe(pc);
    CHECK("stripe lock of the checker is free afterwards", 1);
    [pc release];
    [t release];
}

/* (a) The GSListener pattern from NSRegisterServicesProvider: an NSProxy
 * subclass's class object as a notification observer, registered twice
 * (registerAsServiceProvider retries under a new name after a failure),
 * then a notification posted to it (objc_loadWeakRetained,
 * NSNotificationCenter.m:1138). */
static void caseCenterClassObserver(void)
{
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    id cls = [ClassObserverProxy class];
    int round;
    for (round = 0; round < 2; round++) {
        BOOL raised = NO;
        NS_DURING
            [nc addObserver: cls selector: @selector(note:)
                       name: @"DAR450Class" object: nil];
        NS_HANDLER
            raised = YES;
            printf("     raised %s: %s\n", [[localException name] UTF8String],
                   [[localException reason] UTF8String]);
        NS_ENDHANDLER
        CHECK(round ? "second addObserver of the NSProxy class does not raise"
                    : "addObserver of an NSProxy subclass's class object does not raise",
              !raised);
    }
    [nc postNotificationName: @"DAR450Class" object: nil];
    CHECK("the class observer receives the notification (once per registration)",
          classNotes == 2);
    [nc removeObserver: cls];
    touchStripe(cls);
    CHECK("stripe lock of the class observer is free afterwards", 1);
}

/* (a) An NSProxy instance as a notification observer; the notification is
 * forwarded to its target. */
static void caseCenterProxyObserver(void)
{
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    Target *t = [[Target alloc] init];
    ForwardingProxy *p = [[ForwardingProxy alloc] initWithTarget: t];
    BOOL raised = NO;
    NS_DURING
        [nc addObserver: p selector: @selector(note:) name: @"DAR450Proxy" object: nil];
        [nc postNotificationName: @"DAR450Proxy" object: nil];
    NS_HANDLER
        raised = YES;
        printf("     raised %s: %s\n", [[localException name] UTF8String],
               [[localException reason] UTF8String]);
    NS_ENDHANDLER
    CHECK("addObserver + post with an NSProxy observer does not raise", !raised);
    CHECK("the proxy observer's target receives the notification", t->notes == 1);
    [nc removeObserver: p];
    touchStripe(p);
    CHECK("stripe lock of the proxy observer is free afterwards", 1);
    [p release];
    [t release];
}

/* (a) NSConnection: the object Terminal's second NSRegisterServicesProvider
 * attempt aborted in (-[NSConnection initWithReceivePort:sendPort:] adds
 * observers), created after a GSListener-style registration. */
static void caseConnectionAfterClassObserver(void)
{
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    id cls = [ClassObserverProxy class];
    NS_DURING
        [nc addObserver: cls selector: @selector(note:)
                   name: NSConnectionDidDieNotification object: nil];
    NS_HANDLER
    NS_ENDHANDLER
    int i;
    for (i = 0; i < 16; i++) {
        NSConnection *c = [[NSConnection alloc] initWithReceivePort: [NSPort port]
                                                           sendPort: nil];
        [c invalidate];
        [c release];
    }
    CHECK("16 NSConnections created after an NSProxy-class observer", 1);
    [nc removeObserver: cls];
}

static struct { const char *name; void (*fn)(void); } cases[] = {
    { "proxy-class", caseProxyClass },
    { "proxy-instance", caseProxyInstance },
    { "protocol-checker", caseProtocolChecker },
    { "center-class-observer", caseCenterClassObserver },
    { "center-proxy-observer", caseCenterProxyObserver },
    { "connection-after-class-observer", caseConnectionAfterClassObserver },
};
#define NCASES (sizeof cases / sizeof cases[0])

int main(int argc, char **argv)
{
    unsigned i;
    if (argc == 2) {
        for (i = 0; i < NCASES; i++) {
            if (strcmp(argv[1], cases[i].name) == 0) {
                NSAutoreleasePool *pool = [NSAutoreleasePool new];
                cases[i].fn();
                [pool release];
                return failures ? 1 : 0;
            }
        }
        fprintf(stderr, "unknown case %s\n", argv[1]);
        return 2;
    }
    int failed = 0;
    for (i = 0; i < NCASES; i++) {
        printf("== %s\n", cases[i].name);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            execl(argv[0], argv[0], cases[i].name, (char *)0);
            _exit(127);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) {
            printf("FAIL %s: child killed by signal %d%s\n", cases[i].name, WTERMSIG(st),
                   WTERMSIG(st) == SIGTRAP ? " (recursive os_unfair_lock: a SideTable lock leaked)" : "");
            failed++;
        } else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("FAIL %s: child exit status %d\n", cases[i].name, WEXITSTATUS(st));
            failed++;
        }
        fflush(stdout);
    }
    printf("weak_proxy_smoketest: %s (%d of %u cases failed)\n",
           failed ? "FAIL" : "PASS", failed, (unsigned)NCASES);
    return failed ? 1 : 0;
}
