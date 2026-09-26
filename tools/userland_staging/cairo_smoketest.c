/* cairo_smoketest (DAR-384, X11GS 3): renders text through the whole
 * font/2D stack -- fontconfig (config + font matching) -> FreeType (glyph
 * rasterising) -> cairo (image surface, pixman compositing) -> libpng +
 * zlib (cairo_surface_write_to_png) -- and checks the result.
 *
 *   cairo_smoketest [out.png]      (default /tmp/cairo_smoketest.png)
 *
 * Paths are explicit on purpose: getenv() returns NULL in dynamic
 * executables here (DAR-403), so nothing may depend on HOME/TMPDIR/
 * FONTCONFIG_FILE. With FONTCONFIG_FILE unset fontconfig reads its
 * compiled-in /etc/fonts/fonts.conf, which is what this test wants.
 *
 * Deterministic rendering: grey antialiasing, no hinting, no hint
 * metrics, so the pixels depend only on the font file and the
 * rasteriser, not on fontconfig's hinting rules. build_cairo_smoketest.sh
 * renders the same scene on the host (Homebrew cairo, same DejaVu files)
 * for comparison.
 *
 * Prints "cairo_smoketest: PASS" only if every step works, the PNG
 * round-trips through libpng bit-exactly, and the text actually put ink
 * on the surface.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <pixman.h>
#include <png.h>
#include <zlib.h>
#ifndef CAIRO_SMOKETEST_HOST
#include <expat.h>
#include <X11/Xft/Xft.h>
#endif

#define W 360
#define H 96
static const char *TEXT = "Hello, Darwin! 0123";

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t fnv1a(const unsigned char *p, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void set_ft_face(cairo_t *cr, const char *name, double size)
{
    FcPattern *p = FcNameParse((const FcChar8 *)name);
    cairo_font_face_t *ff = cairo_ft_font_face_create_for_pattern(p);
    cairo_set_font_face(cr, ff);
    cairo_set_font_size(cr, size);
    cairo_font_face_destroy(ff);
    FcPatternDestroy(p);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/cairo_smoketest.png";

    /* --- versions: proves each dylib loaded and bound --- */
    FT_Library ft;
    FT_Int ftmaj = 0, ftmin = 0, ftpat = 0;
    CHECK(FT_Init_FreeType(&ft) == 0, "FT_Init_FreeType");
    FT_Library_Version(ft, &ftmaj, &ftmin, &ftpat);
    printf("versions: cairo %s, pixman %s, freetype %d.%d.%d, fontconfig %d, libpng %s, zlib %s",
           cairo_version_string(), pixman_version_string(), ftmaj, ftmin, ftpat,
           FcGetVersion(), png_get_libpng_ver(NULL), zlibVersion());
#ifndef CAIRO_SMOKETEST_HOST
    printf(", expat %s, Xft %d", XML_ExpatVersion(), XftGetVersion());
#endif
    printf("\n");

    /* --- fontconfig: config, font list, match --- */
    CHECK(FcInit(), "FcInit (could not load /etc/fonts/fonts.conf?)");
    FcPattern *all = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, (char *)0);
    FcFontSet *fs = FcFontList(NULL, all, os);
    int nfonts = fs ? fs->nfont : 0;
    int dejavu = 0;
    for (int i = 0; fs && i < fs->nfont; i++) {
        FcChar8 *fam = NULL;
        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch &&
            strstr((const char *)fam, "DejaVu"))
            dejavu++;
    }
    printf("fontconfig: %d fonts, %d DejaVu\n", nfonts, dejavu);
    CHECK(dejavu > 0, "no DejaVu fonts found by fontconfig");
    if (fs) FcFontSetDestroy(fs);
    FcObjectSetDestroy(os);
    FcPatternDestroy(all);

    FcPattern *pat = FcNameParse((const FcChar8 *)"DejaVu Sans");
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    FcChar8 *file = NULL, *family = NULL;
    if (match) {
        FcPatternGetString(match, FC_FILE, 0, &file);
        FcPatternGetString(match, FC_FAMILY, 0, &family);
    }
    printf("match \"DejaVu Sans\": %s (%s)\n", file ? (char *)file : "(none)", family ? (char *)family : "?");
    CHECK(file && strstr((const char *)file, "DejaVuSans.ttf"), "fontconfig did not match DejaVuSans.ttf");

    /* --- cairo: image surface + toy text API (fontconfig + FreeType backend) --- */
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    cairo_set_font_options(cr, fo);
    /* Both lines go through cairo's FreeType backend explicitly
     * (cairo_ft_font_face_create_for_pattern: fontconfig resolves the
     * pattern, FreeType rasterises), so the host reference render --
     * whose toy API would pick Quartz on macOS -- draws the same way. */
    set_ft_face(cr, "DejaVu Sans", 28);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_move_to(cr, 12, 40);
    cairo_show_text(cr, TEXT);
    set_ft_face(cr, "DejaVu Sans Mono:bold", 18);
    cairo_set_source_rgb(cr, 0.1, 0.2, 0.7);
    cairo_move_to(cr, 12, 78);
    cairo_show_text(cr, "cairo + FreeType + fontconfig");
    CHECK(cairo_scaled_font_get_type(cairo_get_scaled_font(cr)) == CAIRO_FONT_TYPE_FT,
          "pattern face is not a FreeType font");
#ifndef CAIRO_SMOKETEST_HOST
    /* cairo's toy API (cairo_select_font_face), which is what most
     * clients use, must also land on the fontconfig/FreeType backend
     * here -- it is the only font backend this cairo is built with. */
    {
        cairo_surface_t *s2 = cairo_image_surface_create(CAIRO_FORMAT_A8, 8, 8);
        cairo_t *c2 = cairo_create(s2);
        cairo_select_font_face(c2, "DejaVu Serif", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(c2, 12);
        int t = cairo_scaled_font_get_type(cairo_get_scaled_font(c2));
        printf("toy API scaled font type: %d (FT=%d)\n", t, CAIRO_FONT_TYPE_FT);
        CHECK(t == CAIRO_FONT_TYPE_FT, "toy API is not using the FreeType font backend");
        cairo_destroy(c2);
        cairo_surface_destroy(s2);
    }
#endif
    CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "cairo status %s", cairo_status_to_string(cairo_status(cr)));
    cairo_surface_flush(s);

    /* --- ink: the text must have changed pixels --- */
    unsigned char *data = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    int ink = 0, x0 = W, y0 = H, x1 = -1, y1 = -1;
    uint32_t hash = 2166136261u;
    for (int y = 0; y < H; y++) {
        uint32_t *row = (uint32_t *)(data + y * stride);
        hash = fnv1a((const unsigned char *)row, W * 4, hash);
        for (int x = 0; x < W; x++)
            if (row[x] != 0xffffffffu) {
                ink++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    }
    printf("ink: %d of %d pixels, bbox (%d,%d)-(%d,%d), fnv1a %08x\n", ink, W * H, x0, y0, x1, y1, hash);
    CHECK(ink > 1000, "almost no ink: text did not render");

    /* --- libpng + zlib: write, read back, compare --- */
    cairo_status_t st = cairo_surface_write_to_png(s, out);
    CHECK(st == CAIRO_STATUS_SUCCESS, "write_to_png(%s): %s", out, cairo_status_to_string(st));
    cairo_surface_t *back = cairo_image_surface_create_from_png(out);
    st = cairo_surface_status(back);
    CHECK(st == CAIRO_STATUS_SUCCESS, "create_from_png(%s): %s", out, cairo_status_to_string(st));
    if (st == CAIRO_STATUS_SUCCESS) {
        cairo_surface_flush(back);
        unsigned char *bd = cairo_image_surface_get_data(back);
        int bs = cairo_image_surface_get_stride(back);
        int same = cairo_image_surface_get_width(back) == W && cairo_image_surface_get_height(back) == H;
        for (int y = 0; same && y < H; y++)
            same = memcmp(data + y * stride, bd + y * bs, W * 4) == 0;
        CHECK(same, "PNG round trip changed the pixels");
        printf("png: wrote and re-read %s, round trip %s\n", out, same ? "exact" : "DIFFERS");
    }
    FILE *f = fopen(out, "rb");
    long sz = 0;
    if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
    printf("png size: %ld bytes\n", sz);

    cairo_surface_destroy(back);
    cairo_font_options_destroy(fo);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    FcPatternDestroy(pat);
    if (match) FcPatternDestroy(match);
    FT_Done_FreeType(ft);

    printf("cairo_smoketest: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
