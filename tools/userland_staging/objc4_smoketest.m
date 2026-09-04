/*
 * DAR-236: minimal, standalone Objective-C smoketest for the real
 * libobjc.A.dylib built by build_objc4.sh. One @interface/@implementation
 * of one class with one method, confirming real message dispatch
 * (objc_msgSend, class registration, +alloc/-init off libobjc's own
 * real NSObject.mm root class) actually works end to end -- the first
 * real Objective-C program run anywhere in this project's history.
 */
#import <objc/objc.h>
#import <objc/runtime.h>
#import <objc/NSObject.h>

@interface Greeter : NSObject
- (int)add:(int)a to:(int)b;
@end

@implementation Greeter
- (int)add:(int)a to:(int)b
{
	return a + b;
}
@end

extern int printf(const char *, ...);

int main(void)
{
	Greeter *g = [[Greeter alloc] init];
	if (g == 0) {
		printf("objc4 smoketest: FAIL (alloc/init returned nil)\n");
		return 1;
	}

	int result = [g add:2 to:2];
	printf("objc4 smoketest: [g add:2 to:2] = %d\n", result);

	Class cls = object_getClass(g);
	const char *name = class_getName(cls);
	printf("objc4 smoketest: class name = %s\n", name);

	if (result == 4 && name != 0 && name[0] == 'G') {
		printf("objc4 smoketest: PASS\n");
		return 0;
	}
	printf("objc4 smoketest: FAIL\n");
	return 1;
}
