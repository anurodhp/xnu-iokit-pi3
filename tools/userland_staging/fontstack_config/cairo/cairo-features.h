/* cairo-1.18.4 cairo-features.h (DAR-384): stands in for src/meson.build's
 * configure_file(output: 'cairo-features.h', configuration: feature_conf).
 * Surfaces the ticket asks for: image (:638) and xlib (+xrender, :351,
 * :408). png (:244-245) is on for cairo_surface_write_to_png, and
 * meson.build turns the SVG surface on together with it (:244, the
 * 'cairo-png'/'cairo-svg' pair at :246-257) -- that is upstream's own
 * coupling, not a choice made here. No gobject, GL, quartz, xcb, PS/PDF,
 * script or tee. */
#pragma once
#define CAIRO_FEATURES_H                /* :650 */
#define CAIRO_HAS_FC_FONT 1             /* :292 */
#define CAIRO_HAS_FT_FONT 1             /* :313 */
#define CAIRO_HAS_IMAGE_SURFACE 1       /* :638 */
#define CAIRO_HAS_MIME_SURFACE 1        /* :653 */
#define CAIRO_HAS_OBSERVER_SURFACE 1    /* :655 */
#define CAIRO_HAS_PNG_FUNCTIONS 1       /* :245 */
#define CAIRO_HAS_RECORDING_SURFACE 1   /* :654 */
#define CAIRO_HAS_SVG_SURFACE 1         /* :244 */
#define CAIRO_HAS_USER_FONT 1           /* :651 */
#define CAIRO_HAS_XLIB_SURFACE 1        /* :351 */
#define CAIRO_HAS_XLIB_XRENDER_SURFACE 1 /* :408 */
