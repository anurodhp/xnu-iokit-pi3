/*
 * dispmanx_snapshot: what the Pi's display really shows -- the VideoCore
 * HVS's composition of every dispmanx layer (the X framebuffer and any
 * video overlay, e.g. mpv's rpi-overlay under Venice) -- as a PPM file.
 * Neither X (xwd) nor the framebuffer can see an overlay layer.
 *
 *   dispmanx_snapshot out.ppm [display]          (display 0: the default)
 *
 * vc_dispmanx_snapshot into an RGB888 resource the size of the display,
 * read back with vc_dispmanx_resource_read_data (the Raspberry Pi
 * userland's dispmanx client in libbcm_host, over VCHIQ). Built by
 * build_hwdec_smoketest.sh.
 */
#include <stdio.h>
#include <stdlib.h>

#include "bcm_host.h"

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: dispmanx_snapshot out.ppm [display]\n");
		return 2;
	}
	int num = argc > 2 ? atoi(argv[2]) : 0;
	bcm_host_init();
	DISPMANX_DISPLAY_HANDLE_T d = vc_dispmanx_display_open((uint32_t)num);
	if (d == DISPMANX_NO_HANDLE) {
		fprintf(stderr, "dispmanx_snapshot: cannot open display %d\n", num);
		return 1;
	}
	DISPMANX_MODEINFO_T info;
	if (vc_dispmanx_display_get_info(d, &info) != 0) {
		fprintf(stderr, "dispmanx_snapshot: no display info\n");
		return 1;
	}
	int w = info.width, h = info.height, pitch = (w * 3 + 31) & ~31;
	uint32_t image;
	DISPMANX_RESOURCE_HANDLE_T res = vc_dispmanx_resource_create(VC_IMAGE_RGB888, (uint32_t)w, (uint32_t)h, &image);
	if (res == DISPMANX_NO_HANDLE) {
		fprintf(stderr, "dispmanx_snapshot: cannot create a %dx%d resource\n", w, h);
		return 1;
	}
	if (vc_dispmanx_snapshot(d, res, DISPMANX_NO_ROTATE) != 0) {
		fprintf(stderr, "dispmanx_snapshot: snapshot failed\n");
		return 1;
	}
	unsigned char *buf = malloc((size_t)pitch * h);
	VC_RECT_T rect;
	vc_dispmanx_rect_set(&rect, 0, 0, (uint32_t)w, (uint32_t)h);
	if (buf == NULL || vc_dispmanx_resource_read_data(res, &rect, buf, pitch) != 0) {
		fprintf(stderr, "dispmanx_snapshot: read-back failed\n");
		return 1;
	}
	FILE *f = fopen(argv[1], "wb");
	if (f == NULL) {
		perror(argv[1]);
		return 1;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; y++)
		fwrite(buf + (size_t)y * pitch, 3, (size_t)w, f);
	fclose(f);
	vc_dispmanx_resource_delete(res);
	vc_dispmanx_display_close(d);
	printf("dispmanx_snapshot: display %d, %dx%d -> %s\n", num, w, h, argv[1]);
	return 0;
}
