# Hardware video decoding (VPU H.264 / MPEG-4 over VCHIQ)

Status, 2026-10-17: **works on a real Pi 3** (10.0.0.58, firmware start.elf
of the image). Measured there:

- `mmal_example_basic_1`: the VPU's `vc.ril.video_decode` reports
  1280x720 I420 output and decodes the test clip (62 frames in its
  500-iteration loop, exit 0).
- `hwdec_compare` on the 720p clip: VPU 113 fps against FFmpeg's 93 (NEON,
  four threads); 297 frames paired, every sample within 1 of FFmpeg's
  (0.064% off by 1; the VPU is not bit-exact, its I420 is converted from
  its own format). PASS.
- Venice, 720p clip, GL renderer, `ao=null`: decoder `mmal-copy`; whole-run
  CPU time 6.2 s user instead of 16.9 s (1280x720 window) and 6.5 s instead
  of 14.6 s (854x480) for the same 12-second clip. Frames drawn per second
  are unchanged: at 1280x720 the GL draw (34 ms/frame) is the limit either
  way.
- GL present on a presenter thread (`mesa_xlib_vc4/xlib_vc4.c`, opt out
  with `XLIB_VC4_SYNC=1`): the same run draws in 28-30 ms/frame, 260-271
  frames shown instead of 238-245 (26.5 ms and ~290 with Xorg's
  `ShadowFB off`, which is not the default: reads from the framebuffer
  are ~30 MB/s). What is left is memory bandwidth: ~32 MB of copies per
  frame plus ~500 MB/s of 1920x1080 scanout; overlapped, each copy runs
  slower (read-back 6 -> 10 ms, put 7 -> 13-19 ms, mpv's render 12 ->
  20 ms). Tools: `build_gl_video_bench.sh` (a video frame's GL cost step by
  step) and `build_sample_prof.sh` (an injected sampling profiler;
  `SAMPLE_PROF_WALL=1` gives the main thread's wall-clock stacks).
- Under QEMU (no VPU) the decoder declines and mpv decodes in software.

Found on the Pi and fixed: the fragment line size (see Open risks, now
resolved). Still open: the end of a stream (below).

## The path

```
Venice (mpv hwdec=mmal-copy, hwdec-codecs=h264,mpeg4)
  -> FFmpeg h264_mmal / mpeg4_mmal          libavcodec (build_ffmpeg.sh, --enable-mmal)
  -> MMAL: vc.ril.video_decode              libmmal.dylib  ┐
  -> VCHI / VCHIQ client                    libvchiq_arm   │ third_party/userland,
  -> vcos                                   libvcos        │ fork anurodhp/userland iokit-port
  -> bcm_host_init()                        libbcm_host    ┘ (build_rpi_userland.sh)
  -> IOConnectCallMethod("BCM2837VCHIQ")    BCM2837VCHIQUserClient
  -> VCHIQ core (Linux's, vendored)         iokit/BCM2837VCHIQ/vchiq/
  -> shared slots + doorbells + mailbox     BCM2837VCHIQ.cpp
  -> VideoCore firmware (start.elf)
```

Linux reaches the same decoder through the same MMAL component (its
bcm2835-codec V4L2 driver is a kernel-side MMAL client). The VPU decodes into
its own memory and copies each picture into the client's buffer by DMA (a
VCHIQ bulk receive); mpv takes it as an ordinary YUV 4:2:0 image.

## Kernel: BCM2837VCHIQ

- `iokit/BCM2837VCHIQ/vchiq/`: Linux's `vchiq_core.c`, `vchiq_arm.c`,
  `vchiq_dev.c` and headers from raspberrypi/linux rpi-6.6.y
  (`bba53a1`), dual GPL-2.0/BSD-3-Clause, used under BSD-3. Changes are
  limited to the platform half of `vchiq_arm.c` (moved to
  `BCM2837VCHIQ.cpp`), the file operations of `vchiq_dev.c` (now entry
  points for the user client; arguments arrive as kernel copies), and
  `remote_event.__unused` (an attribute macro on XNU: it silently dropped the
  field and shrank a shared-memory struct). `vchiq_xnu.h`/`.c` implement the
  Linux API the core uses (mutexes, completions, wait queues, RCU, kthreads).
- `BCM2837VCHIQ.cpp`: the slots (contiguous, write-combined, below 1 GiB,
  VPU bus address = physical | 0xC0000000), `RPI_FIRMWARE_VCHIQ_INIT`
  through BCM2837Mailbox, the doorbell interrupt (basic IRQ 2 = vector 66),
  and bulk pagelists for client memory (wired with IOMemoryDescriptor,
  cleaned / invalidated by VA through the physical aperture, 4 KiB VPU pages,
  64-byte "fragments" for partial cache lines at the ends of a receive).
- `BCM2837VCHIQUserClient`: one VCHIQ instance per connection; selector =
  the ioctl's number, ABI in `BCM2837VCHIQUAPI.h`.
- DT node `vchiq` only in `rpi3-realhw.dts` (`RPI3_HAVE_VCHIQ`). Boot-args:
  `vchiq=0` off, `vchiq-log=<0-7>` the core's log level.

## Userland

The fork's changes (see its commit): VCHIQ over the IOKit service; Darwin
semaphores for vcos; and a 64-bit MMAL client, which upstream does not build
for arm64 at all because the VideoCore worker's messages carry 32-bit
pointers. On LP64, host pointers VideoCore only echoes are 32-bit handles
(`mmal_vc_handle.c`), structs embedded in messages are explicit 32-bit
images, and FORMAT_CHANGED event payloads are rewritten to the host layout.
`build_rpi_userland.sh` gates on `mmal_abi_check/`: clang's record layouts
of every message struct, for 32-bit ARM Linux and for this arm64 host, must
be identical (35 records, 0 differ).

`config.txt` has `gpu_mem=128` (was 64): the decoder allocates its frames
from VideoCore memory. XNU gets 896 MB instead of 960.

## Testing on a Pi

Install on a running Pi: the kernel, `devicetrees/rpi3-realhw.dtb` (as
`rpi3.dtb`) and `sdcard/config.txt` go on the FAT boot partition
(`mount -t msdos /dev/disk0s1 /tmp/boot`); libvcos, libvchiq_arm,
libbcm_host, libmmal and the FFmpeg dylibs at their install names, the
smoketests in /usr/local/bin, Venice.app in /Applications.

1. Build: kernel, `build_rpi_userland.sh`, `build_ffmpeg.sh`,
   `build_hwdec_smoketest.sh`, `build_venice.sh`; real-hardware image or
   deploy. The boot log should have
   `BCM2837VCHIQ: slots at bus 0x... , 64 fragments, pagelist pool on`.
2. `/usr/local/bin/mmal_example_basic_1 /usr/local/share/hwdec/hwdec_test_720p.h264`
   (upstream's MMAL example, no FFmpeg): prints the output port format, then
   "decoded frame" lines.
3. `/usr/local/bin/hwdec_compare /usr/local/share/hwdec/hwdec_test_720p.mp4`:
   software and VPU decodes must be bit-identical (exit 0); prints both rates.
4. Venice: Video Info's Decoder line reads "hardware, VideoCore VPU
   (mmal-copy)"; `-VeniceQuitAtEnd YES` logs `hwdec-current`.

## Open risks (unverified until the first Pi run)

- **Cache line size: resolved, 32.** The pagelist fragments first assumed
  64-byte lines (Linux's `brcm,bcm2836-vchiq` drvdata). On the Pi the last
  32 bytes but 8 of every frame came back stale; the firmware, given no
  Linux DT with a `cache-line-size` property, uses its compatibility value
  of 32 (Linux vchiq_arm.c's comment), and with 32 the frames are right.
- **End of stream: the last pictures are lost.** The VPU signals
  end-of-stream before the last pictures of a file: 272 of 300 in
  hwdec_compare's flat-out pass (the last 28 in display order, the same
  number with up to 1 s of delay before the EOS), 297 when frames are
  consumed more slowly, and Venice drew about 20 fewer frames than in
  software. The VPU returned 275 frame buffers for 301 input buffers
  (trace, `VC_LOGLEVEL=mmalipc:trace`), so they are not lost in transfer.
  Not yet known whether Linux with the same firmware loses them too.
- **vchostif services under 64-bit.** `bcm_host_init()` also opens the
  dispmanx, tvservice, gencmd and cec services; upstream builds those for
  arm64, but they have not been exercised here.
- **Access control.** Any process can open BCM2837VCHIQ (Linux restricts
  /dev/vchiq to the video group). VCHIQ reaches every firmware service,
  gencmd included.
- **No teardown.** The firmware cannot be told to forget the slots, so the
  driver never frees them and its threads run until shutdown.
