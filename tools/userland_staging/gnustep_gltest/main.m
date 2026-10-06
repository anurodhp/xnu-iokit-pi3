/*
 * GLTest.app: an AppKit OpenGL check for this port. A window holding an
 * NSOpenGLView draws a clear and a fixed-function triangle; the pixels are
 * read back with glReadPixels and the app prints "GSGLTEST PASS" or
 * "GSGLTEST FAIL n" and exits. That exercises gnustep-back's GLX glue
 * (XGGLContext/XGGLFormat) on top of Mesa's libGL: softpipe, or the V3D
 * with the MESA_VC4=1 libGL.
 *
 *   GLTest.app/GLTest [-frames N] [-keep]
 *     -frames N  after the check, redraw N frames (a rotating triangle) and
 *                print the frame rate, flushBuffer included
 *     -keep      stay open after the check (for looking at it on a screen)
 */
#import <AppKit/AppKit.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
static void check(BOOL ok, NSString *what)
{
	printf("%s %s\n", ok ? "ok:  " : "FAIL:", [what UTF8String]);
	fflush(stdout);
	if (!ok)
		fails++;
}

@interface GLTestView : NSOpenGLView
{
@public
	float angle;
	BOOL checked;
}
@end

@implementation GLTestView
- (void) drawRect: (NSRect)r
{
	NSSize s = [self bounds].size;
	[[self openGLContext] makeCurrentContext];
	glViewport(0, 0, (GLsizei)s.width, (GLsizei)s.height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(angle, 0, 0, 1);
	glClearColor(0.f, 0.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColor3f(0.f, 1.f, 0.f);
	glBegin(GL_TRIANGLES);
	glVertex2f(-1.f, -1.f);
	glVertex2f(1.f, -1.f);
	glVertex2f(-1.f, 1.f);
	glEnd();
	if (!checked) {
		checked = YES;
		unsigned char p[4];
		glFinish();
		printf("GL_RENDERER %s\nGL_VERSION  %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
		/* lower-left quarter is inside the triangle, upper-right is not */
		glReadPixels((GLint)(s.width / 4), (GLint)(s.height / 4), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
		check(p[0] == 0 && p[1] == 255 && p[2] == 0,
		    [NSString stringWithFormat: @"triangle pixel is green (got %d,%d,%d)", p[0], p[1], p[2]]);
		glReadPixels((GLint)(s.width * 3 / 4), (GLint)(s.height * 3 / 4), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
		check(p[0] == 0 && p[1] == 0 && p[2] == 255,
		    [NSString stringWithFormat: @"background pixel is the clear colour (got %d,%d,%d)", p[0], p[1], p[2]]);
		check(glGetError() == GL_NO_ERROR, @"no GL error");
	}
	[[self openGLContext] flushBuffer];
}
@end

@interface GLTestController : NSObject
{
	NSWindow *window;
	GLTestView *view;
	int frames;
	BOOL keep;
}
@end

@implementation GLTestController
- (void) applicationDidFinishLaunching: (NSNotification *)n
{
	NSArray *args = [[NSProcessInfo processInfo] arguments];
	NSUInteger i = [args indexOfObject: @"-frames"];
	if (i != NSNotFound && i + 1 < [args count])
		frames = [[args objectAtIndex: i + 1] intValue];
	keep = [args containsObject: @"-keep"];

	NSOpenGLPixelFormatAttribute attrs[] = {
		NSOpenGLPFADoubleBuffer, NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 16, 0
	};
	NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes: attrs];
	check(pf != nil, @"NSOpenGLPixelFormat (double buffer, 24-bit colour, 16-bit depth)");
	if (pf == nil) {
		[self finish];
		return;
	}
	NSRect frame = NSMakeRect(40, 40, 128, 128);
	window = [[NSWindow alloc] initWithContentRect: frame
	    styleMask: NSTitledWindowMask backing: NSBackingStoreBuffered defer: NO];
	[window setTitle: @"GLTest"];
	view = [[GLTestView alloc] initWithFrame: NSMakeRect(0, 0, 128, 128) pixelFormat: pf];
	check([view openGLContext] != nil, @"NSOpenGLContext created");
	[window setContentView: view];
	[window makeKeyAndOrderFront: nil];
	[view display];
	/* the first draw may wait for the window to be mapped */
	[self performSelector: @selector(afterFirstDraw) withObject: nil afterDelay: 0.5];
	[self performSelector: @selector(timeout) withObject: nil afterDelay: 30.0];
}

- (void) afterFirstDraw
{
	if (!view->checked) {
		[view display];
		[self performSelector: @selector(afterFirstDraw) withObject: nil afterDelay: 0.5];
		return;
	}
	if (frames > 0) {
		NSDate *t0 = [NSDate date];
		for (int f = 0; f < frames; f++) {
			view->angle += 3.f;
			[view display];
		}
		double dt = -[t0 timeIntervalSinceNow];
		printf("frames: %d in %.2f s = %.1f fps\n", frames, dt, frames / dt);
	}
	if (keep)
		return;
	[self finish];
}

- (void) timeout
{
	if (!view->checked)
		check(NO, @"the view was drawn within 30 s");
	if (!keep || !view->checked)
		[self finish];
}

- (void) finish
{
	printf(fails ? "GSGLTEST FAIL %d\n" : "GSGLTEST PASS\n", fails);
	fflush(stdout);
	exit(fails != 0);
}
@end

int
main(int argc, const char **argv)
{
	NSAutoreleasePool *pool = [NSAutoreleasePool new];
	[NSApplication sharedApplication];
	[NSApp setDelegate: [GLTestController new]];
	[NSApp run];
	[pool release];
	return 0;
}
