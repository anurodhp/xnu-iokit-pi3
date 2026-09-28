/*
 * DAR-382: the runtime features a GNUstep-style Foundation needs, exercised
 * on the real objc4 libobjc.A.dylib (the runtime chosen for GNUstep, see
 * build_gnustep_objc4_smoketest.sh's header for the decision).
 *
 * No Foundation exists yet, so this is written the way gnustep-base must sit
 * on objc4: libobjc owns the NSObject root class, and the Foundation layer
 * adds behaviour to it through categories. That is Apple's own layering:
 * objc4's -[NSObject description] returns nil and is marked "Replaced by CF
 * (returns an NSString)" (third_party/objc4/runtime/NSObject.mm:2452-2460).
 *
 * Each check prints one line; the last line is PASS only if all passed.
 */
#import <objc/objc.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <objc/objc-exception.h>
#import <objc/NSObject.h>
#include <Block.h>

extern int printf(const char *, ...);
extern int strcmp(const char *, const char *);

/* objc4's autorelease pool and ARC entry points, exported by libobjc
 * (third_party/objc4/runtime/NSObject.mm) but declared only in private
 * headers. */
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);
extern id objc_autorelease(id);
extern id objc_retain(id);
extern void objc_release(id);
extern id objc_storeWeak(id *, id);
extern id objc_loadWeakRetained(id *);
extern void objc_destroyWeak(id *);
extern void objc_setForwardHandler(void *, void *);
extern void _objc_init(void);

static int failures;
static void check(const char *what, int ok)
{
	printf("gnustep-objc4 smoketest: %-34s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* ---- load/initialize ordering ------------------------------------ */
static int loadOrder, initOrder, orderCounter;

/* ---- a minimal string class, standing in for Foundation's -------- */
@interface GSSmokeString : NSObject {
@public
	const char *cstr;
}
+ (id)stringWithCString:(const char *)s;
- (const char *)UTF8String;
@end

@implementation GSSmokeString
+ (id)stringWithCString:(const char *)s
{
	GSSmokeString *str = [[self alloc] init];
	str->cstr = s;
	return objc_autorelease(str);
}
- (const char *)UTF8String
{
	return cstr;
}
@end

/* ---- the Foundation layer: a category on libobjc's own NSObject --- */
@interface NSObject (GSSmokeFoundation)
- (id)description;
@end

@implementation NSObject (GSSmokeFoundation)
- (id)description
{
	return [GSSmokeString stringWithCString:class_getName(object_getClass(self))];
}
@end

/* ---- a second root class, the way GNUstep's NSProxy is one -------- */
__attribute__((objc_root_class))
@interface GSSmokeRoot {
	Class isa;
}
+ (id)alloc;
+ (Class)class;
- (id)init;
- (int)answer;
@end

@implementation GSSmokeRoot
+ (void)initialize
{
	if (initOrder == 0) {
		initOrder = ++orderCounter;
	}
}
+ (void)load
{
	loadOrder = ++orderCounter;
}
+ (id)alloc
{
	return class_createInstance(self, 0);
}
+ (Class)class
{
	return self;
}
- (id)init
{
	return self;
}
- (int)answer
{
	return 41;
}
@end

@interface GSSmokeRoot (Category)
- (int)answerPlusOne;
@end

@implementation GSSmokeRoot (Category)
- (int)answerPlusOne
{
	return [self answer] + 1;
}
@end

/* ---- exceptions, dealloc tracking, forwarding --------------------- */
static int deallocs;

@interface GSSmokeException : NSObject
@end
@implementation GSSmokeException
@end

@interface GSSmokeTracked : NSObject
@end
@implementation GSSmokeTracked
- (void)dealloc
{
	deallocs++;
	[super dealloc];
}
@end

static SEL forwardedSel;
static id forwardedSelf;
static id smokeForwardHandler(id self, SEL sel)
{
	forwardedSelf = self;
	forwardedSel = sel;
	return self;
}

@interface NSObject (GSSmokeUnimplemented)
- (id)gsSmokeNotImplemented;
@end

/* On Apple, libSystem's initializer reaches _objc_init() through
 * libdispatch_init() -> _os_object_init() (third_party/libdispatch/
 * src/object.m:70-72). Until DAR-237/DAR-408 this port's libdispatch was
 * built without ObjC, where _os_object_init() is empty (src/init.c:
 * 1541-1546), so nothing called it. With --objc-init this test calls it
 * itself, to check the rest of the runtime independently of libdispatch.
 *
 * It has to happen from an initializer, not from main(): dyld-940 makes
 * its RuntimeState read-only once initializers have run
 * (third_party/dyld/dyld/dyldMain.cpp:866-867, state.decWritable()), and
 * _dyld_objc_notify_register() -> RuntimeState::setObjCNotifiers() stores
 * the three callbacks into that state (dyld/DyldRuntimeState.cpp:1520-1522)
 * without making it writable. Called from main() that store is a write to
 * a read-only page: SIGBUS, esr 0x9200004f (DAR-236, 2026-09-28). The main
 * executable's own initializers run inside
 * runInitializersBottomUpPlusUpwardLinks(), which holds incWritable()
 * (dyld/Loader.cpp:1311). The executable's +load has already been
 * dispatched by then (Loader.cpp:1302, before its initializers), so in
 * this mode +load does not run for this image. */
static int selfInit;
__attribute__((constructor))
static void smokeObjcInitFromInitializer(int argc, char **argv)
{
	selfInit = (argc > 1 && strcmp(argv[1], "--objc-init") == 0);
	if (selfInit) {
		printf("gnustep-objc4 smoketest: calling _objc_init() itself from an initializer (--objc-init)\n");
		_objc_init();
	}
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	void *pool = objc_autoreleasePoolPush();

	/* +load runs at image load, +initialize at the first message. */
	if (selfInit) {
		printf("gnustep-objc4 smoketest: %-34s %s\n", "+load ran before main", "skipped (--objc-init)");
		orderCounter = 1;
	} else {
		check("+load ran before main", loadOrder == 1 && initOrder == 0);
	}
	GSSmokeRoot *r = [[GSSmokeRoot alloc] init];
	check("+initialize on first message", initOrder == 2);

	/* Second root class and a category on it. */
	check("root class +alloc/-init", r != nil && [r answer] == 41);
	check("category method", [r answerPlusOne] == 42);

	/* Category on libobjc's NSObject replaces -description. */
	GSSmokeTracked *t = [[GSSmokeTracked alloc] init];
	GSSmokeString *d = [t description];
	check("-description via NSObject category",
	    d != nil && strcmp([d UTF8String], "GSSmokeTracked") == 0);

	/* A typed IMP call. objc4 forces typed dispatch prototypes on arm64
	 * (objc4 runtime/objc-api.h:121-126): an uncast IMP call passes
	 * arguments where a variadic callee looks for them, which on Apple
	 * arm64 is the stack, not registers. gnustep-base calls IMPs uncast;
	 * every such site needs this cast on this port. */
	int (*answerIMP)(id, SEL) = (int (*)(id, SEL))
	    class_getMethodImplementation(object_getClass(r), @selector(answer));
	check("typed IMP call", answerIMP(r, @selector(answer)) == 41);

	/* Exceptions: objc4 throws through the C++ ABI
	 * (runtime/objc-exception.mm:560-590, __cxa_allocate_exception). */
	int caught = 0, finallyRan = 0;
	@try {
		@throw [[GSSmokeException alloc] init];
	} @catch (GSSmokeTracked *wrong) {
		caught = -1;
	} @catch (GSSmokeException *e) {
		caught = (object_getClass(e) == [GSSmokeException class]);
	} @finally {
		finallyRan = 1;
	}
	check("@try/@catch/@finally", caught == 1 && finallyRan == 1);

	/* Blocks: a copied heap block with a __block variable. */
	__block int counter = 10;
	int (^inc)(int) = Block_copy(^(int by) { counter += by; return counter; });
	inc(5);
	check("block with __block capture", inc(1) == 16 && counter == 16);
	Block_release(inc);

	/* A block as a method IMP: imp_implementationWithBlock() maps
	 * trampoline pages from /usr/lib/libobjc-trampolines.dylib
	 * (objc4 runtime/objc-block-trampolines.mm:173). */
	/* The block captures no object: with libdispatch's Block callbacks
	 * installed (_Block_use_RR2, libdispatch/src/object.m:73-80), copying a
	 * block retains captured objects through objc_retain, and GSSmokeRoot,
	 * a root class, has no -retain. */
	IMP blockIMP = imp_implementationWithBlock(^int(id self_) {
		return self_ != nil ? 7 : -1;
	});
	class_addMethod(object_getClass(r), @selector(gsSmokeBlockMethod),
	    blockIMP, "i@:");
	int (*blockCall)(id, SEL) = (int (*)(id, SEL))
	    class_getMethodImplementation(object_getClass(r), @selector(gsSmokeBlockMethod));
	check("imp_implementationWithBlock", blockCall(r, @selector(gsSmokeBlockMethod)) == 7);

	/* Associated objects. */
	static char key;
	objc_setAssociatedObject(t, &key, d, OBJC_ASSOCIATION_RETAIN);
	check("associated object", objc_getAssociatedObject(t, &key) == d);
	objc_setAssociatedObject(t, &key, nil, OBJC_ASSOCIATION_RETAIN);

	/* Forwarding: gnustep-base's NSInvocation forwarding hooks in here. */
	/* arm64 has no separate struct-return forwarder (objc4
	 * runtime/objc-config.h:142-146, SUPPORT_STRET 0), so the second
	 * argument is ignored; pass the same handler, not NULL. */
	objc_setForwardHandler((void *)smokeForwardHandler, (void *)smokeForwardHandler);
	id fw = [t gsSmokeNotImplemented];
	check("objc_setForwardHandler",
	    fw == t && forwardedSelf == t && forwardedSel == @selector(gsSmokeNotImplemented));

	/* Weak references are cleared at dealloc. */
	id weak = nil;
	objc_storeWeak(&weak, t);
	id strong = objc_loadWeakRetained(&weak);
	check("weak ref loads while alive", strong == t);
	objc_release(strong);
	[t release];
	check("dealloc on last release", deallocs == 1);
	check("weak ref zeroed after dealloc", objc_loadWeakRetained(&weak) == nil);
	objc_destroyWeak(&weak);

	/* Autorelease pool pop frees what it holds. */
	void *inner = objc_autoreleasePoolPush();
	objc_autorelease([[GSSmokeTracked alloc] init]);
	check("object alive inside pool", deallocs == 1);
	objc_autoreleasePoolPop(inner);
	check("autorelease pool pop", deallocs == 2);

	objc_autoreleasePoolPop(pool);

	if (failures == 0) {
		printf("gnustep-objc4 smoketest: PASS\n");
		return 0;
	}
	printf("gnustep-objc4 smoketest: FAIL (%d)\n", failures);
	return 1;
}
