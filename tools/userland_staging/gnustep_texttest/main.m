/*
 * TextTest.app: checks gnustep-gui/back Unicode text (UTF-32 glyphs and
 * fallback fonts). A window draws one row each of Latin, CJK, hangul,
 * Hebrew, emoji above U+FFFF and a lone "?" in white on black; the window
 * contents are read back (initWithFocusedViewRect:) and the lit pixels per
 * row counted, so the result can be read without a screen. A missing glyph
 * draws as "?" or a box, so a row that has real shapes lights well over
 * twice as many pixels as the "?" row. /tmp/texttest.png is the read-back.
 * (Offscreen NSBitmapImageRep contexts do not work on the X11 cairo
 * backend: flushGraphics crashes and nothing lands in the rep, so this
 * reads the window.) The window title is CJK + emoji, for looking at the
 * Eau titlebar; -keep leaves the window up.
 *
 *   TextTest.app/TextTest [-keep]
 * Prints "GSTEXTTEST PASS" or "GSTEXTTEST FAIL n".
 */
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <string.h>

#define ROWS 6
#define ROWH 40
static NSString *rows[ROWS] = {
	@"Hello",
	@"中文日本語",
	@"한국어",
	@"שלום",
	@"\U0001F600\U0001F680",
	@"?",
};
static const char *names[ROWS] = { "latin", "cjk", "hangul", "hebrew", "emoji", "question-mark" };

static int fails;
static void check(BOOL ok, NSString *what)
{
	printf("%s %s\n", ok ? "ok:  " : "FAIL:", [what UTF8String]);
	fflush(stdout);
	if (!ok)
		fails++;
}

@interface TextView : NSView
@end
@implementation TextView
- (BOOL) isFlipped
{
	return YES;
}
- (void) drawRect: (NSRect)r
{
	NSDictionary *a = [NSDictionary dictionaryWithObjectsAndKeys:
		[NSFont systemFontOfSize: 24], NSFontAttributeName,
		[NSColor whiteColor], NSForegroundColorAttributeName, nil];
	int i;

	[[NSColor blackColor] set];
	NSRectFill([self bounds]);
	for (i = 0; i < ROWS; i++)
		[rows[i] drawAtPoint: NSMakePoint(4, i * ROWH + 4) withAttributes: a];
}
@end

int main(int argc, char **argv)
{
	CREATE_AUTORELEASE_POOL(pool);
	NSWindow *win;
	TextView *view;
	NSBitmapImageRep *rep;
	int counts[ROWS], i, x, y;
	BOOL keep = argc > 1 && !strcmp(argv[1], "-keep");

	[NSApplication sharedApplication];
	win = [[NSWindow alloc] initWithContentRect: NSMakeRect(100, 100, 320, ROWS * ROWH)
		styleMask: NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask
		backing: NSBackingStoreBuffered defer: NO];
	[win setTitle: @"中文日本語 \U0001F600 TextTest"];
	view = [[TextView alloc] initWithFrame: NSMakeRect(0, 0, 320, ROWS * ROWH)];
	[win setContentView: view];
	[win makeKeyAndOrderFront: nil];
	[win display];
	[view lockFocus];
	rep = [[NSBitmapImageRep alloc] initWithFocusedViewRect: [view bounds]];
	[view unlockFocus];
	check(rep != nil, @"read back the window");
	if (rep) {
		int w = [rep pixelsWide], h = [rep pixelsHigh], spp = [rep samplesPerPixel];
		unsigned char *p = [rep bitmapData];

		printf("rep %dx%d spp %d\n", w, h, spp);
		for (i = 0; i < ROWS; i++) {
			counts[i] = 0;
			for (y = i * ROWH; y < (i + 1) * ROWH && y < h; y++)
				for (x = 0; x < w; x++)
					if (p[y * [rep bytesPerRow] + x * spp] > 64)
						counts[i]++;
			printf("ink %-14s %d\n", names[i], counts[i]);
		}
		check(counts[0] > 0, @"latin draws");
		check(counts[1] > 2 * counts[5], @"CJK draws more than '?' would");
		check(counts[2] > 2 * counts[5], @"hangul draws more than '?' would");
		check(counts[3] > counts[5], @"hebrew draws");
		check(counts[4] > 2 * counts[5], @"emoji (above U+FFFF) draws more than '?' would");
		check([[rep representationUsingType: NSPNGFileType properties: nil]
			writeToFile: @"/tmp/texttest.png" atomically: NO], @"wrote /tmp/texttest.png");
	}
	printf(fails ? "GSTEXTTEST FAIL %d\n" : "GSTEXTTEST PASS\n", fails);
	fflush(stdout);
	if (keep)
		[NSApp run];
	RELEASE(pool);
	return fails ? 1 : 0;
}
