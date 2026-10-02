/*
 * foundation_smoketest (DAR-388): gnustep-base's Foundation on Apple's
 * objc4 runtime. Each check prints "ok" or "FAIL"; the last line is
 * "foundation_smoketest: PASS" only if every check passed.
 *
 * The pieces of the objc4 port it covers (see the anurodhp/libs-base
 * iokit-port commits): NSConstantString literals
 * (-fconstant-string-class=NSConstantString), the NSObject(GSFoundation)
 * category on libobjc's NSObject, NSAllocateObject/retain counts through
 * objc4, the objc_setForwardHandler trampoline (GSFFIInvocation.m) behind
 * -forwardInvocation:, NSInvocation through libffi, exceptions, and
 * NSThread/NSLock.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int failures;
#define CHECK(name, cond) do { \
        int ok_ = (cond) ? 1 : 0; \
        printf("%s %s\n", ok_ ? "ok  " : "FAIL", name); \
        fflush(stdout); \
        if (!ok_) failures++; \
    } while (0)

/* Receives messages it does not implement and forwards them to target. */
@interface Forwarder : NSObject
{
    id target;
@public
    int forwarded;
}
- (id) initWithTarget: (id)t;
@end
@implementation Forwarder
- (id) initWithTarget: (id)t { if ((self = [super init])) target = [t retain]; return self; }
- (void) dealloc { [target release]; [super dealloc]; }
- (NSMethodSignature *) methodSignatureForSelector: (SEL)s
{
    NSMethodSignature *m = [super methodSignatureForSelector: s];
    return m ? m : [target methodSignatureForSelector: s];
}
- (void) forwardInvocation: (NSInvocation *)inv
{
    forwarded++;
    [inv invokeWithTarget: target];
}
@end

@interface Adder : NSObject
- (long) add: (long)a to: (long)b;
- (double) scale: (double)x by: (float)f;
- (NSRange) rangeFrom: (NSUInteger)loc length: (NSUInteger)len;
@end
@implementation Adder
- (long) add: (long)a to: (long)b { return a + b; }
- (double) scale: (double)x by: (float)f { return x * f; }
- (NSRange) rangeFrom: (NSUInteger)loc length: (NSUInteger)len { return NSMakeRange(loc, len); }
@end

@interface Worker : NSObject
{
@public
    NSLock *lock;
    int count;
}
- (void) run: (id)arg;
@end
@implementation Worker
- (void) run: (id)arg
{
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    int i;
    for (i = 0; i < 1000; i++) {
        [lock lock];
        count++;
        [lock unlock];
    }
    [pool release];
}
@end

int main(int argc, char **argv)
{
    NSAutoreleasePool *pool = [NSAutoreleasePool new];

    /* Constant strings and formatting. */
    NSString *lit = @"hello";
    CHECK("constant string class", [lit isKindOfClass: [NSString class]]);
    CHECK("constant string length", [lit length] == 5);
    NSString *f = [NSString stringWithFormat: @"%@ %d %.2f %s %@", lit, 42, 3.14159, "c", [NSNumber numberWithInt: 7]];
    CHECK("stringWithFormat", [f isEqualToString: @"hello 42 3.14 c 7"]);
    CHECK("UTF8String", strcmp([[lit uppercaseString] UTF8String], "HELLO") == 0);
    CHECK("componentsSeparatedByString",
          [[@"a,b,c" componentsSeparatedByString: @","] count] == 3);

    /* Collections and NSNumber. */
    NSMutableArray *a = [NSMutableArray array];
    int i;
    for (i = 0; i < 100; i++) [a addObject: [NSNumber numberWithInt: i]];
    CHECK("NSMutableArray count", [a count] == 100);
    long sum = 0;
    for (NSNumber *n in a) sum += [n intValue];
    CHECK("fast enumeration", sum == 4950);
    [a sortUsingSelector: @selector(compare:)];
    CHECK("sortUsingSelector", [[a objectAtIndex: 99] intValue] == 99);
    NSDictionary *d = [NSDictionary dictionaryWithObjectsAndKeys:
        @"one", [NSNumber numberWithInt: 1], @"two", [NSNumber numberWithInt: 2], nil];
    CHECK("NSDictionary lookup", [[d objectForKey: [NSNumber numberWithInt: 2]] isEqual: @"two"]);
    NSString *desc = [[NSArray arrayWithObjects: @"x", @"y", nil] description];
    CHECK("NSArray description", [desc rangeOfString: @"x"].location != NSNotFound);
    NSMutableSet *set = [NSMutableSet setWithObjects: @"p", @"q", @"p", nil];
    CHECK("NSSet dedup", [set count] == 2);

    /* Mutation during enumeration raises (objc_enumerationMutation handler). */
    BOOL raised = NO;
    @try {
        for (id o in a) { (void)o; [a addObject: @"z"]; }
    } @catch (NSException *e) {
        raised = [[e name] isEqualToString: NSGenericException];
    }
    CHECK("enumeration mutation raises", raised);

    /* Exceptions. */
    NSString *reason = nil;
    BOOL finally = NO;
    @try {
        [NSException raise: NSInvalidArgumentException format: @"bad %d", 3];
    } @catch (NSException *e) {
        reason = [e reason];
    } @finally {
        finally = YES;
    }
    CHECK("@try/@catch NSException", [reason isEqualToString: @"bad 3"]);
    CHECK("@finally", finally);
    raised = NO;
    @try { [a objectAtIndex: 1000000]; }
    @catch (NSException *e) { raised = [[e name] isEqualToString: NSRangeException]; }
    CHECK("NSRangeException", raised);

    /* Retain counts through objc4. */
    NSObject *o = [NSObject new];
    [o retain];
    CHECK("retainCount", [o retainCount] == 2);
    [o release];
    [o release];

    /* NSObject(GSFoundation) on libobjc's NSObject. */
    CHECK("NSObject description", [[[[NSObject new] autorelease] description] hasPrefix: @"<NSObject"]);
    raised = NO;
    @try { [[[NSObject new] autorelease] performSelector: @selector(noSuchMethod)]; }
    @catch (NSException *e) { raised = [[e name] isEqualToString: NSInvalidArgumentException]; }
    CHECK("doesNotRecognizeSelector raises", raised);

    /* NSInvocation built by hand (libffi ffi_call). */
    Adder *adder = [[Adder new] autorelease];
    NSMethodSignature *sig = [adder methodSignatureForSelector: @selector(add:to:)];
    NSInvocation *inv = [NSInvocation invocationWithMethodSignature: sig];
    long x = 40, y = 2, r = 0;
    [inv setSelector: @selector(add:to:)];
    [inv setArgument: &x atIndex: 2];
    [inv setArgument: &y atIndex: 3];
    [inv invokeWithTarget: adder];
    [inv getReturnValue: &r];
    CHECK("NSInvocation invoke", r == 42);

    /* Forwarding: objc4 -> gs_objc4_forward trampoline -> libffi closure
       -> -forwardInvocation:. Integer, floating point and struct returns. */
    Forwarder *fw = [[[Forwarder alloc] initWithTarget: adder] autorelease];
    CHECK("forward long", [(id)fw add: 5 to: 6] == 11);
    CHECK("forward double/float", [(id)fw scale: 1.5 by: 4.0f] == 6.0);
    NSRange rg = [(id)fw rangeFrom: 3 length: 9];
    CHECK("forward struct", rg.location == 3 && rg.length == 9);
    CHECK("forwardInvocation count", fw->forwarded == 3);

    /* NSDate / NSProcessInfo / NSFileManager / NSUserDefaults. */
    NSDate *now = [NSDate date];
    CHECK("NSDate", [now timeIntervalSince1970] > 1.0e9);
    CHECK("NSProcessInfo processName", [[[NSProcessInfo processInfo] processName] length] > 0);
    NSArray *ls = [[NSFileManager defaultManager] contentsOfDirectoryAtPath: @"/usr" error: NULL];
    CHECK("NSFileManager /usr", [ls containsObject: @"lib"]);
    NSString *tmp = [NSString stringWithFormat: @"/tmp/foundation_smoketest.%d",
                     [[NSProcessInfo processInfo] processIdentifier]];
    CHECK("writeToFile", [@"data" writeToFile: tmp atomically: YES]);
    CHECK("stringWithContentsOfFile",
          [[NSString stringWithContentsOfFile: tmp] isEqualToString: @"data"]);
    [[NSFileManager defaultManager] removeItemAtPath: tmp error: NULL];
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    [ud setObject: @"v" forKey: @"foundation_smoketest"];
    CHECK("NSUserDefaults", [[ud stringForKey: @"foundation_smoketest"] isEqual: @"v"]);

    /* Property lists. */
    id pl = [@"{ a = (1, 2); b = \"x y\"; }" propertyList];
    CHECK("propertyList parse", [pl isKindOfClass: [NSDictionary class]] &&
          [[pl objectForKey: @"a"] count] == 2);

    /* Threads. */
    Worker *w = [[Worker new] autorelease];
    w->lock = [[NSLock new] autorelease];
    NSMutableArray *threads = [NSMutableArray array];
    for (i = 0; i < 4; i++) {
        NSThread *t = [[[NSThread alloc] initWithTarget: w selector: @selector(run:) object: nil] autorelease];
        [threads addObject: t];
        [t start];
    }
    for (i = 0; i < 500; i++) {
        BOOL done = YES;
        for (NSThread *t in threads) if (![t isFinished]) done = NO;
        if (done) break;
        [NSThread sleepForTimeInterval: 0.01];
    }
    [w->lock lock];
    int count = w->count;
    [w->lock unlock];
    CHECK("NSThread + NSLock", count == 4000);

    /* Blocks are objects (GSBlocks.m builds __NS{Stack,Malloc,Global}Block__
     * over libclosure's isa buffers): messaging them must not crash. */
    {
        __block int hits = 0;
        int base = 5;
        void (^global)(void) = ^{ };
        void (^stack)(void) = ^{ hits += base; };
        id g = (id)global, st = (id)stack;
        CHECK("global block is an object", [g isKindOfClass: [NSObject class]]);
        CHECK("global block class", strcmp(class_getName(object_getClass(g)), "__NSGlobalBlock__") == 0);
        CHECK("global block retain", [g retain] == g);
        [g release];
        id heap = [st copy];
        CHECK("stack block copy -> malloc block",
              heap != st && strcmp(class_getName(object_getClass(heap)), "__NSMallocBlock__") == 0);
        ((void (^)(void))heap)();
        CHECK("copied block runs", hits == 5);
        CHECK("malloc block retain/release", [heap retain] == heap);
        [heap release]; [heap release];
        NSMutableArray *ba = [NSMutableArray array];
        [ba addObject: ^{ hits += 100; }];
        ((void (^)(void))[ba objectAtIndex: 0])();
        CHECK("block stored in NSArray", hits == 105);
        NSTimer *tm = [[NSTimer scheduledTimerWithTimeInterval: 0.01 repeats: NO
                                                         block: ^(NSTimer *t) { hits += 1000; }] retain];
        [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: 0.3]];
        CHECK("NSTimer block fires", hits >= 1105 && ![tm isValid]);
        [tm release];
        CHECK("@[] and @{} singletons", [@[] count] == 0 && [@{} count] == 0);
    }

    [pool release];
    printf("foundation_smoketest: %s (%d failed)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
