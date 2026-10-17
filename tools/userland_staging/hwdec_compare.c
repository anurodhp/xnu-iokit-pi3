/*
 * hwdec_compare: decodes a file's video with FFmpeg's software decoder and
 * with its MMAL one (the Pi's VPU, through BCM2837VCHIQ), and compares the
 * pictures pixel by pixel.
 *
 *   hwdec_compare <file> [max-frames]
 *
 * What a correct VPU path looks like, measured on a real Pi 3 (2026-10-17,
 * the 720p testsrc2 clip below): every picture within 1 of FFmpeg's in
 * every sample, about 0.1% of samples off by 1. The VPU's video_decode
 * component is not bit-exact with the reference decoder (its I420 output is
 * converted from its own internal format), so 1 is the tolerance. A broken
 * transfer looks different: a stale cache line, a wrong page in a bulk
 * pagelist or a mishandled partial-line "fragment" at a buffer's ends shows
 * up as runs of bytes off by tens (the first such bug found here: 32 stale
 * bytes at the end of every frame, fragments sized for 64-byte lines when
 * the firmware uses 32).
 *
 * Three passes over the file: software alone and hardware alone, timed
 * (frames per second of wall time, demuxing included), then both decoders
 * fed the same packets, their pictures paired by pts and compared over the
 * visible area. Exit status 0 if at least one pair was compared and no
 * sample differed by more than 1.
 *
 * The hardware pass usually ends a few pictures early, all at the end of
 * the stream (the timed pass's "missing" line lists their pts): measured
 * 272 of 300 in the timed pass, 297 in the paired one. The VPU returns
 * end-of-stream before them; their pictures are not wrong, they are never
 * delivered (docs/video-decode.md).
 *
 * HWDEC_DUMP=<dir>: writes the first pair as <dir>/h264.yuv and
 * <dir>/h264_mmal.yuv (visible area, planes one after another).
 * HWDEC_VERBOSE=1: FFmpeg's debug log.
 *
 * Built by build_hwdec_smoketest.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

#define QMAX 64

struct dec {
	AVCodecContext *cc;
	AVFrame *q[QMAX];
	int n;
	int produced;
	int done;           /* the decoder returned AVERROR_EOF */
};

static double
now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int
open_input(const char *path, AVFormatContext **fc)
{
	*fc = NULL;
	if (avformat_open_input(fc, path, NULL, NULL) < 0 || avformat_find_stream_info(*fc, NULL) < 0) {
		fprintf(stderr, "%s: cannot open\n", path);
		return -1;
	}
	return av_find_best_stream(*fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
}

static AVCodecContext *
open_decoder(AVFormatContext *fc, int si, const char *name)
{
	const AVCodec *codec = avcodec_find_decoder_by_name(name);
	AVCodecContext *cc;
	int err;
	if (codec == NULL) {
		fprintf(stderr, "no decoder %s in this FFmpeg\n", name);
		return NULL;
	}
	cc = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(cc, fc->streams[si]->codecpar);
	cc->pkt_timebase = fc->streams[si]->time_base;
	cc->thread_count = 0;
	if ((err = avcodec_open2(cc, codec, NULL)) < 0) {
		fprintf(stderr, "%s: avcodec_open2: %s\n", name, av_err2str(err));
		avcodec_free_context(&cc);
		return NULL;
	}
	return cc;
}

/* Pass 1 and 2: one decoder alone, up to max pictures; returns the count. */
static int64_t *timed_pts[2];

static int
timed(const char *path, const char *name, int max, double *secs, int which)
{
	AVFormatContext *fc;
	int si = open_input(path, &fc), n = 0, eof = 0;
	AVCodecContext *cc = si < 0 ? NULL : open_decoder(fc, si, name);
	AVPacket *pkt = av_packet_alloc();
	AVFrame *fr = av_frame_alloc();
	if (cc == NULL) {
		return -1;
	}
	double t0 = now();
	while (n < max) {
		if (!eof) {
			if (av_read_frame(fc, pkt) < 0) {
				eof = 1;
				avcodec_send_packet(cc, NULL);
			} else {
				if (pkt->stream_index == si) {
					avcodec_send_packet(cc, pkt);
				}
				av_packet_unref(pkt);
			}
		}
		int err;
		while (n < max && (err = avcodec_receive_frame(cc, fr)) == 0) {
			timed_pts[which] = realloc(timed_pts[which], sizeof(int64_t) * (size_t)(n + 1));
			timed_pts[which][n] = fr->best_effort_timestamp;
			n++;
			av_frame_unref(fr);
		}
		if (eof && err != AVERROR(EAGAIN) && err != 0) {
			break;
		}
	}
	*secs = now() - t0;
	avcodec_free_context(&cc);
	avformat_close_input(&fc);
	av_packet_free(&pkt);
	av_frame_free(&fr);
	return n;
}

static void
drain(struct dec *d)
{
	AVFrame *fr = av_frame_alloc();
	int err = 0;
	while (d->n < QMAX && (err = avcodec_receive_frame(d->cc, fr)) == 0) {
		d->q[d->n++] = av_frame_clone(fr);
		d->produced++;
		av_frame_unref(fr);
	}
	if (err != 0 && err != AVERROR(EAGAIN)) {
		d->done = 1;
	}
	av_frame_free(&fr);
}

static void
pop(struct dec *d, int i)
{
	av_frame_free(&d->q[i]);
	memmove(&d->q[i], &d->q[i + 1], sizeof(d->q[0]) * (size_t)(d->n - i - 1));
	d->n--;
}

static void
dump(const AVFrame *f, const char *name)
{
	const char *dir = getenv("HWDEC_DUMP");
	const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(f->format);
	char path[512];
	FILE *fp;
	if (dir == NULL || d == NULL) {
		return;
	}
	snprintf(path, sizeof(path), "%s/%s.yuv", dir, name);
	if ((fp = fopen(path, "wb")) == NULL) {
		return;
	}
	for (int p = 0; p < 3 && f->data[p] != NULL; p++) {
		int w = p ? AV_CEIL_RSHIFT(f->width, d->log2_chroma_w) : f->width;
		int rows = p ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
		for (int y = 0; y < rows; y++) {
			fwrite(f->data[p] + (ptrdiff_t)y * f->linesize[p], 1, (size_t)w, fp);
		}
	}
	fclose(fp);
}

static uint64_t total_samples, differing;
static int max_diff;

/* Compares two pictures over the visible area; returns their largest
 * sample difference, or 256 if they are not the same shape. */
static int
compare(const AVFrame *a, const AVFrame *b)
{
	const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(a->format);
	int worst = 0;
	if (a->format != b->format || a->width != b->width || a->height != b->height || d == NULL ||
	    (d->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
		return 256;
	}
	for (int p = 0; p < 3 && a->data[p] != NULL; p++) {
		int w = p ? AV_CEIL_RSHIFT(a->width, d->log2_chroma_w) : a->width;
		int rows = p ? AV_CEIL_RSHIFT(a->height, d->log2_chroma_h) : a->height;
		for (int y = 0; y < rows; y++) {
			const uint8_t *ra = a->data[p] + (ptrdiff_t)y * a->linesize[p];
			const uint8_t *rb = b->data[p] + (ptrdiff_t)y * b->linesize[p];
			for (int x = 0; x < w; x++) {
				int dd = abs(ra[x] - rb[x]);
				if (dd != 0) {
					differing++;
					if (dd > worst) {
						worst = dd;
					}
				}
			}
			total_samples += (uint64_t)w;
		}
	}
	return worst;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: hwdec_compare <file> [max-frames]\n");
		return 2;
	}
	const char *path = argv[1];
	if (getenv("HWDEC_VERBOSE") != NULL) {
		av_log_set_level(AV_LOG_DEBUG);
	}
	int max = argc > 2 ? atoi(argv[2]) : 300;
	AVFormatContext *fc;
	int si = open_input(path, &fc);
	if (si < 0) {
		fprintf(stderr, "%s: no video stream\n", path);
		return 2;
	}
	enum AVCodecID id = fc->streams[si]->codecpar->codec_id;
	const char *sw = id == AV_CODEC_ID_H264 ? "h264" : id == AV_CODEC_ID_MPEG4 ? "mpeg4" : NULL;
	const char *hw = id == AV_CODEC_ID_H264 ? "h264_mmal" : id == AV_CODEC_ID_MPEG4 ? "mpeg4_mmal" : NULL;
	if (sw == NULL) {
		fprintf(stderr, "%s: not H.264 or MPEG-4 Part 2\n", path);
		return 2;
	}
	int w = fc->streams[si]->codecpar->width, h = fc->streams[si]->codecpar->height;

	double ta = 0, tb = 0;
	int na = timed(path, sw, max, &ta, 0);
	int nb = timed(path, hw, max, &tb, 1);
	if (na <= 0 || nb <= 0) {
		fprintf(stderr, "decode failed (software %d pictures, hardware %d)\n", na, nb);
		return 1;
	}
	printf("software %-10s %4d pictures %dx%d in %.2f s: %.1f fps\n", sw, na, w, h, ta, na / ta);
	printf("hardware %-10s %4d pictures %dx%d in %.2f s: %.1f fps\n", hw, nb, w, h, tb, nb / tb);
	if (nb < na) {
		/* which software pictures the hardware pass did not produce */
		int shown = 0;
		printf("  missing from the hardware pass (pts):");
		for (int i = 0; i < na && shown < 40; i++) {
			int k;
			for (k = 0; k < nb && timed_pts[1][k] != timed_pts[0][i]; k++) {
			}
			if (k == nb) {
				printf(" %lld", (long long)timed_pts[0][i]);
				shown++;
			}
		}
		printf("\n");
	}

	/* Pass 3: both decoders on the same packets, compared as they come. */
	struct dec a = { open_decoder(fc, si, sw) }, b = { open_decoder(fc, si, hw) };
	if (a.cc == NULL || b.cc == NULL) {
		return 1;
	}
	AVPacket *pkt = av_packet_alloc();
	int progressed, pairs = 0, identical = 0, unpaired = 0, eof = 0, dumped = 0;
	for (;;) {
		if (!eof) {
			if (av_read_frame(fc, pkt) < 0) {
				eof = 1;
				avcodec_send_packet(a.cc, NULL);
				avcodec_send_packet(b.cc, NULL);
			} else {
				if (pkt->stream_index == si) {
					avcodec_send_packet(a.cc, pkt);
					avcodec_send_packet(b.cc, pkt);
				}
				av_packet_unref(pkt);
			}
		}
		drain(&a);
		drain(&b);
		progressed = 0;
		while (b.n > 0) {
			int64_t pts = b.q[0]->best_effort_timestamp, j;
			for (j = 0; j < a.n && a.q[j]->best_effort_timestamp != pts; j++) {
			}
			if (j == a.n) {
				if (eof || a.n == QMAX) {
					unpaired++;         /* the software twin never came */
					pop(&b, 0);
					progressed = 1;
					continue;
				}
				break;
			}
			int d = compare(a.q[j], b.q[0]);
			if (!dumped) {
				dump(a.q[j], sw);
				dump(b.q[0], hw);
				dumped = 1;
			}
			pairs++;
			if (d == 0) {
				identical++;
			}
			if (d > max_diff) {
				max_diff = d;
				if (d > 1) {
					printf("  pts %lld: samples off by up to %d\n", (long long)pts, d);
				}
			}
			/* software pictures before this one have no hardware twin
			 * (the VPU dropped them); let them go */
			while (j-- >= 0) {
				pop(&a, 0);
			}
			pop(&b, 0);
			progressed = 1;
		}
		(void)progressed;
		if (b.done && b.n == 0) {
			break;      /* software pictures left over have no hardware twin */
		}
		if (pairs >= max) {
			break;
		}
	}
	printf("%d pairs compared: %d identical, largest difference %d, %.3f%% of samples differ%s\n",
	    pairs, identical, max_diff, total_samples ? 100.0 * (double)differing / (double)total_samples : 0.0,
	    unpaired ? " (some hardware pictures had no software twin)" : "");
	printf("%s\n", pairs > 0 && max_diff <= 1 ? "PASS" : "FAIL");
	return pairs > 0 && max_diff <= 1 ? 0 : 1;
}
