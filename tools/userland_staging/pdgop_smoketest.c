/*
 * DAR-358: PureDarwin's PDGOP framebuffer path against this port's
 * IOBootFramebuffer (IOGraphics' real IOFramebufferUserClient, DAR-331).
 *
 * PDGOPOpen() (third_party/PureDarwin src/Libraries/PDGOP/PDGOP.c, built
 * unmodified by build_pdgop_smoketest.sh) finds the framebuffer service,
 * opens user client type 0, reads the current display mode (selector 2)
 * and pixel information (selector 1, kIOFBSystemAperture), and maps VRAM
 * (kIOFBVRAMMemory = 110). This prints what it got, fills the screen with
 * eight colour bars built from the reported component masks, reads the
 * pixels back through the mapping, then holds the mapping open for
 * argv[1] seconds (default 30) so a QEMU screendump can be taken.
 *
 * Usage: pdgop_smoketest [hold-seconds]      Exit status 0 = PASS.
 */
#include <PDGOP.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t scale(uint32_t mask, int on)
{
    return on ? mask : 0;
}

int main(int argc, char **argv)
{
    PDGOPFramebuffer fb;
    int hold = argc > 1 ? atoi(argv[1]) : 30;
    kern_return_t kr = PDGOPOpen(&fb);

    if (kr != KERN_SUCCESS) {
        printf("pdgop_smoketest: PDGOPOpen failed at stage %s, kr=0x%x\n",
               PDGOPLastErrorStage(), kr);
        printf("pdgop_smoketest: FAIL\n");
        return 1;
    }
    printf("pdgop_smoketest: mode %ux%u, stride %u, %u bpp, pixelType %u\n",
           fb.width, fb.height, fb.stride, fb.bpp, fb.pixelType);
    printf("pdgop_smoketest: masks R 0x%08x G 0x%08x B 0x%08x\n",
           fb.componentMasks[0], fb.componentMasks[1], fb.componentMasks[2]);
    printf("pdgop_smoketest: VRAM mapped at 0x%llx, size 0x%llx\n",
           (unsigned long long)fb.address, (unsigned long long)fb.size);

    int failures = 0;
    if (fb.bpp != 32 || fb.width == 0 || fb.height == 0 ||
        fb.stride < fb.width * 4 ||
        (uint64_t)fb.stride * fb.height > fb.size) {
        printf("pdgop_smoketest: unexpected mode for a 32bpp fill\n");
        failures++;
    } else {
        /* Bars, left to right: white yellow cyan green magenta red blue black. */
        static const int rgb[8][3] = {
            {1,1,1}, {1,1,0}, {0,1,1}, {0,1,0}, {1,0,1}, {1,0,0}, {0,0,1}, {0,0,0},
        };
        uint32_t bar[8];
        for (int i = 0; i < 8; i++)
            bar[i] = scale(fb.componentMasks[0], rgb[i][0]) |
                     scale(fb.componentMasks[1], rgb[i][1]) |
                     scale(fb.componentMasks[2], rgb[i][2]);
        volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)fb.address;
        for (uint32_t y = 0; y < fb.height; y++) {
            volatile uint32_t *row = (volatile uint32_t *)(base + (uint64_t)y * fb.stride);
            for (uint32_t x = 0; x < fb.width; x++)
                row[x] = bar[(x * 8) / fb.width];
        }
        for (int i = 0; i < 8; i++) {
            uint32_t x = (uint32_t)(((uint64_t)fb.width * (2 * i + 1)) / 16);
            uint32_t y = fb.height / 2;
            uint32_t got = ((volatile uint32_t *)(base + (uint64_t)y * fb.stride))[x];
            if (got != bar[i]) {
                printf("pdgop_smoketest: bar %d at (%u,%u) reads 0x%08x, wrote 0x%08x\n",
                       i, x, y, got, bar[i]);
                failures++;
            }
        }
        printf("pdgop_smoketest: wrote 8 colour bars, read back %s\n",
               failures ? "WRONG" : "ok");
    }

    printf("pdgop_smoketest: holding the mapping for %d s\n", hold);
    fflush(stdout);
    sleep(hold);
    PDGOPClose(&fb);
    printf("pdgop_smoketest: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
