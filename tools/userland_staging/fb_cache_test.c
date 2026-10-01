/*
 * DAR-460: VRAM mapping cache-mode test.
 *
 * Maps the IOBootFramebuffer VRAM exactly the way PDGOP (and so the Xorg
 * driver) does -- IOConnectMapMemory64(kIOFBVRAMMemory, kIOMapAnywhere |
 * <cache mode>) through PDGOPOpenWithCacheMode() -- once per mode named on
 * the command line, and for each mapping measures store/copy/read
 * throughput against a malloc'd RAM buffer of the same size, then checks
 * that the stores landed (read back through the mapping).
 *
 * Kernel side (xnu-7195): a default-cache user map of this range is
 * VM_WIMG_IO = Device-nGnRnE (see docs/framebuffer-mapping.md). User code
 * cannot read its own PTEs, so this program cannot print the attribute; it
 * prints what it asked for and how fast the mapping is. On the real Pi 3
 * the throughput difference between modes is the evidence. Under QEMU TCG
 * memory attributes are not modelled at all (qemu-zhuowei
 * target/arm/helper.c HELPER(dc_zva) comment), so QEMU numbers only show
 * that each mode maps, writes land, and nothing faults.
 *
 * Usage: fb_cache_test [-r reps] [-H hold-secs] [-T band-secs] [mode ...]
 *   mode: default | inhibit | wc | posted     (default: "default wc")
 *   -H n  after the last mode, leave colour bars on screen for n seconds
 *   -T n  after the last mode, write one 8-row band per second for n
 *         seconds (band i at rows 8i..8i+7, value 0x00a50000 | i) and read
 *         it back through the mapping: the DAR-443 QEMU check (compare with
 *         the monitor's `xp` of the physical framebuffer)
 * Exit status 0 = every mode mapped and read back what it wrote.
 */
#include <PDGOP.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint32_t v4u32 __attribute__((vector_size(16), aligned(16)));

static double now_s(void)
{
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0)
        mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

static void store32(volatile uint32_t *p, size_t n, uint32_t v)
{
    for (size_t i = 0; i < n; i++)
        p[i] = v;
}

static void store128(volatile v4u32 *p, size_t n, uint32_t v)
{
    v4u32 q = {v, v, v, v};
    for (size_t i = 0; i < n; i++)
        p[i] = q;
}

/* The same row copy the Xorg driver's shadow blit uses (4-byte head/tail,
 * 16-byte aligned middle, 64 bytes per iteration). */
static void copy_row(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    while (bytes >= 4 && ((uintptr_t)dst & 15)) {
        *(volatile uint32_t *)dst = *(const uint32_t *)src;
        dst += 4; src += 4; bytes -= 4;
    }
    while (bytes >= 64) {
        v4u32 a, b, c, d;
        memcpy(&a, src, 16); memcpy(&b, src + 16, 16);
        memcpy(&c, src + 32, 16); memcpy(&d, src + 48, 16);
        ((volatile v4u32 *)dst)[0] = a; ((volatile v4u32 *)dst)[1] = b;
        ((volatile v4u32 *)dst)[2] = c; ((volatile v4u32 *)dst)[3] = d;
        dst += 64; src += 64; bytes -= 64;
    }
    while (bytes >= 4) {
        *(volatile uint32_t *)dst = *(const uint32_t *)src;
        dst += 4; src += 4; bytes -= 4;
    }
}

static uint32_t read32(volatile const uint32_t *p, size_t n)
{
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++)
        s += p[i];
    return s;
}

/* MB/s of each operation over `bytes`, `reps` times. */
static void bench(const char *what, uint8_t *dst, const uint8_t *src,
                  size_t bytes, size_t stride, uint32_t rows, int reps)
{
    double t0, t;
    volatile uint32_t sink;

    t0 = now_s();
    for (int r = 0; r < reps; r++)
        store32((volatile uint32_t *)dst, bytes / 4, 0x00102030u + r);
    t = now_s() - t0;
    printf("fb_cache_test: %-8s store32  %8.1f MB/s\n", what, bytes * (double)reps / t / 1e6);

    t0 = now_s();
    for (int r = 0; r < reps; r++)
        store128((volatile v4u32 *)dst, bytes / 16, 0x00405060u + r);
    t = now_s() - t0;
    printf("fb_cache_test: %-8s store128 %8.1f MB/s\n", what, bytes * (double)reps / t / 1e6);

    t0 = now_s();
    for (int r = 0; r < reps; r++)
        for (uint32_t y = 0; y < rows; y++)
            copy_row(dst + (size_t)y * stride, src + (size_t)y * stride, stride);
    t = now_s() - t0;
    printf("fb_cache_test: %-8s copy     %8.1f MB/s  (%.2f ms per full frame)\n", what,
           bytes * (double)reps / t / 1e6, t / reps * 1e3);

    t0 = now_s();
    for (int r = 0; r < reps; r++)
        sink = read32((volatile const uint32_t *)dst, bytes / 4);
    t = now_s() - t0;
    (void)sink;
    printf("fb_cache_test: %-8s read32   %8.1f MB/s\n", what, bytes * (double)reps / t / 1e6);
}

static int parse_mode(const char *s, uint32_t *mode)
{
    if (!strcmp(s, "default")) *mode = PDGOP_MAP_DEFAULT_CACHE;
    else if (!strcmp(s, "inhibit")) *mode = PDGOP_MAP_INHIBIT_CACHE;
    else if (!strcmp(s, "wc")) *mode = PDGOP_MAP_WRITE_COMBINE;
    else if (!strcmp(s, "posted")) *mode = PDGOP_MAP_POSTED_WRITE;
    else return 0;
    return 1;
}

static void bars(PDGOPFramebuffer *fb, uint8_t *base)
{
    static const int rgb[8][3] = {
        {1,1,1}, {1,1,0}, {0,1,1}, {0,1,0}, {1,0,1}, {1,0,0}, {0,0,1}, {0,0,0},
    };
    for (uint32_t y = 0; y < fb->height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(base + (size_t)y * fb->stride);
        for (uint32_t x = 0; x < fb->width; x++) {
            const int *c = rgb[(x * 8) / fb->width];
            row[x] = (c[0] ? fb->componentMasks[0] : 0) |
                     (c[1] ? fb->componentMasks[1] : 0) |
                     (c[2] ? fb->componentMasks[2] : 0);
        }
    }
}

int main(int argc, char **argv)
{
    int reps = 4, hold = 0, bandsecs = 0, opt, failures = 0;
    const char *defaults[] = {"default", "wc"};
    const char **modes = defaults;
    int nmodes = 2;

    while ((opt = getopt(argc, argv, "r:H:T:")) != -1) {
        if (opt == 'r') reps = atoi(optarg);
        else if (opt == 'H') hold = atoi(optarg);
        else if (opt == 'T') bandsecs = atoi(optarg);
        else { fprintf(stderr, "usage: fb_cache_test [-r reps] [-H secs] [default|inhibit|wc|posted ...]\n"); return 2; }
    }
    if (optind < argc) {
        modes = (const char **)&argv[optind];
        nmodes = argc - optind;
    }
    if (reps < 1) reps = 1;

    for (int m = 0; m < nmodes; m++) {
        uint32_t mode;
        PDGOPFramebuffer fb;

        if (!parse_mode(modes[m], &mode)) {
            printf("fb_cache_test: unknown mode %s\n", modes[m]);
            failures++;
            continue;
        }
        kern_return_t kr = PDGOPOpenWithCacheMode(&fb, mode);
        if (kr != KERN_SUCCESS) {
            printf("fb_cache_test: mode %s: open failed at %s, kr=0x%x\n",
                   modes[m], PDGOPLastErrorStage(), kr);
            failures++;
            continue;
        }
        size_t bytes = (size_t)fb.stride * fb.height;
        printf("fb_cache_test: mode %s: map options 0x%x, VRAM at 0x%llx size 0x%llx, %ux%u stride %u bpp %u\n",
               modes[m], fb.mapOptions, (unsigned long long)fb.address,
               (unsigned long long)fb.size, fb.width, fb.height, fb.stride, fb.bpp);
        if (fb.bpp != 32 || bytes == 0 || bytes > fb.size || (fb.stride & 15)) {
            printf("fb_cache_test: unexpected geometry\n");
            failures++;
            PDGOPClose(&fb);
            continue;
        }

        uint8_t *ram = NULL, *src = NULL;
        if (posix_memalign((void **)&ram, 64, bytes) || posix_memalign((void **)&src, 64, bytes)) {
            printf("fb_cache_test: out of memory\n");
            return 1;
        }
        for (size_t i = 0; i < bytes / 4; i++)
            ((uint32_t *)src)[i] = (uint32_t)(i * 2654435761u);
        if (m == 0)
            bench("RAM", ram, src, bytes, fb.stride, fb.height, reps);
        bench(modes[m], (uint8_t *)(uintptr_t)fb.address, src, bytes, fb.stride, fb.height, reps);

        /* Correctness: copy a known pattern, read it back through the map. */
        uint8_t *vram = (uint8_t *)(uintptr_t)fb.address;
        for (uint32_t y = 0; y < fb.height; y++)
            copy_row(vram + (size_t)y * fb.stride, src + (size_t)y * fb.stride, fb.stride);
        size_t bad = 0;
        for (size_t i = 0; i < bytes / 4; i += 97)
            if (((volatile uint32_t *)vram)[i] != ((uint32_t *)src)[i])
                bad++;
        printf("fb_cache_test: mode %s: pattern read back %s (%zu bad of %zu sampled)\n",
               modes[m], bad ? "WRONG" : "ok", bad, bytes / 4 / 97 + 1);
        if (bad)
            failures++;

        bars(&fb, vram);
        if (hold > 0 && m == nmodes - 1) {
            printf("fb_cache_test: holding colour bars for %d s\n", hold);
            fflush(stdout);
            sleep(hold);
        }
        if (bandsecs > 0 && m == nmodes - 1) {
            double t0 = now_s();
            for (int i = 0; i < bandsecs && (uint32_t)(i * 8 + 8) <= fb.height; i++) {
                uint32_t v = 0x00a50000u | (uint32_t)i;
                for (uint32_t y = (uint32_t)i * 8; y < (uint32_t)i * 8 + 8; y++)
                    store32((volatile uint32_t *)(vram + (size_t)y * fb.stride), fb.width, v);
                uint32_t got = *(volatile uint32_t *)(vram + (size_t)i * 8 * fb.stride);
                printf("fb_cache_test: band %d t=%.1fs offset 0x%zx wrote 0x%08x read 0x%08x%s\n",
                       i, now_s() - t0, (size_t)i * 8 * fb.stride, v, got, got == v ? "" : " MISMATCH");
                fflush(stdout);
                if (got != v)
                    failures++;
                sleep(1);
            }
        }
        free(ram);
        free(src);
        PDGOPClose(&fb);
    }
    printf("fb_cache_test: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
