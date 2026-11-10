# Device support

Which Raspberry Pi boards this XNU port runs on, and what works on them.

**Short version:** the port targets one board, the **Raspberry Pi 3 Model B**
(BCM2837). It is the only board it has been run on, and everything marked
"Works" below was verified on a real one. Other boards with the same SoC family
are untested. Some will probably boot, but their network chips have no drivers
here. The Pi 4 and Pi 5 families use different SoCs and are not supported.

Status words used here:

- **Works**: verified on a real Pi 3 Model B.
- **Untested**: no one has run the port on it. The notes say what is expected
  to differ.
- **Not supported**: known not to work, because the hardware is different or no
  driver exists.

## Boards

Every Raspberry Pi with a 64-bit (ARMv8) CPU. Boards with 32-bit CPUs only (Pi
1, Zero, Zero W, Pi 2 Model B v1.0/v1.1) cannot run a 64-bit kernel and are not
listed.

| Board | SoC | RAM | Status | Notes |
|---|---|---|---|---|
| **Pi 3 Model B** | BCM2837 | 1 GB | **Works** | The target board. See [Pi 3 Model B](#pi-3-model-b) below. |
| Pi 3 Model B+ | BCM2837B0 | 1 GB | Untested | Same SoC family, so boot, SD, USB, HDMI, GPU and audio should carry over. Not expected to work: Ethernet (LAN7515, gigabit; the driver here is for the 3B's LAN9514) and Wi-Fi/Bluetooth (CYW43455; the drivers here are for the 3B's CYW43438/BCM43430). |
| Pi 3 Model A+ | BCM2837B0 | 512 MB | Untested | No Ethernet. Wi-Fi/Bluetooth are CYW43455 (no driver, as on the 3B+). Its single USB port is wired to the SoC without a hub, a path not exercised on real hardware. 512 MB is tight for the X/GNUstep desktop. |
| Pi 2 Model B v1.2 | BCM2837 | 1 GB | Untested | The Pi 3B's SoC, with the same LAN9514 USB hub and Ethernet, so nearly everything should work with the 3B image as it is. Wi-Fi and Bluetooth stay off unless `wifi=1`/`bt=1` are set, and the 2B has neither. One known difference: its 3.5 mm jack's right channel is PWM1 on GPIO45, where the 3B uses GPIO41 (`BCM2837PWM.cpp` sets GPIO40/41), so audio would play on the left channel only. |
| Zero 2 W | RP3A0 (BCM2710A1) | 512 MB | Untested | The BCM2837 die in a package with its RAM. No Ethernet. The Wi-Fi/Bluetooth module is a CYW43436, not the CYW43438 the drivers here were written and tested against. The USB port connects to the SoC with no hub. 512 MB is tight for the desktop. |
| Compute Module 3 | BCM2837 | 1 GB | Untested | Same SoC as the 3B. Has no Ethernet, Wi-Fi or Bluetooth on the module. Storage is on-module eMMC (or an SD card on CM3 Lite) on the same controller the 3B's SD card uses. Needs its own device tree for the carrier board's wiring. |
| Compute Module 3+ | BCM2837B0 | 1 GB | Untested | As the CM3. |
| Pi 4 Model B | BCM2711 | 1-8 GB | Not supported | Different SoC: a GIC-400 interrupt controller in place of the BCM2836/2837 local and ARM controllers, peripherals at a different address, USB 3 through a VL805 controller on PCIe, GENET Ethernet, a newer V3D GPU and the EMMC2 controller. The port has no drivers for any of these. |
| Pi 400 | BCM2711 | 4 GB | Not supported | As the Pi 4. |
| Compute Module 4 | BCM2711 | 1-8 GB | Not supported | As the Pi 4. |
| Pi 5 | BCM2712 | 2-16 GB | Not supported | Cortex-A76 cores, and nearly all I/O (USB, Ethernet, GPIO) behind the RP1 chip on PCIe. Nothing here applies. |
| Pi 500 | BCM2712 | 8 GB | Not supported | As the Pi 5. |
| Compute Module 5 | BCM2712 | 2-16 GB | Not supported | As the Pi 5. |

## Adding another BCM2837 board

The other Pi 3-class boards have not been worked on yet; none is available to
test. This is what was found, 2026-11-01, about what each would take, so the
work can start from here.

**No new device tree is needed just to boot.** The current real-hardware image
should already boot on any BCM2837 board:

- `devicetrees/rpi3.dts` describes the SoC, which is the same chip on all of
  these boards. Nothing in it is specific to the 3B's board layout except the
  Wi-Fi and Bluetooth wiring.
- That wiring is unused unless `wifi=1` or `bt=1` is set, so a board without
  those chips is not affected.
- The firmware on the card (`sdcard/start.elf`, built 2026-08-06) supports every
  Pi 3 variant, the Pi 2 v1.2 and the Zero 2 W.
- U-Boot passes the memory size the firmware reports (`config.txt`), so 512 MB
  boards need no change.

What separates each board from "works" is mostly networking, which is driver
work rather than a device tree:

| Board | Expected with today's image | What is missing | Work needed |
|---|---|---|---|
| Pi 2 Model B v1.2 | Nearly everything, including Ethernet (the same LAN9514) | The right audio channel: the 2B wires PWM1 to GPIO45, where the 3B uses GPIO41 | Small. `BCM2837PWM` would read the PWM1 pin from the device tree instead of hard-coding GPIO41 (`BCM2837PWM.cpp`, the GPIO40/41 ALT0 setup), and a new `devicetrees/rpi2-realhw.dts` would set 45. |
| Pi 3 Model B+ | Boot, USB, HDMI, GPU, video decode, audio (the same GPIO40/41) | Ethernet (LAN7515); Wi-Fi and Bluetooth (CYW43455) | Large. A new USB Ethernet driver for the LAN7515. Wi-Fi for the 43455: the driver accepts only chip ID 43430 (`BCM43430Wifi.cpp`), and the 43455 needs its own firmware, NVRAM and CLM blob. Bluetooth: `BCM43430Bluetooth` checks the BCM43430A1's LMP subversion before loading its patch, and the 43455 has its own patch. |
| Pi 3 Model A+ | Boot, HDMI, GPU, audio. USB untested: its one port has no hub | Wi-Fi and Bluetooth (CYW43455); no Ethernet at all | As for the 3B+ (without Ethernet). Until then it has no network. |
| Compute Module 3 / 3+ | Boot from the on-module eMMC, HDMI, GPU | Nothing on the module; audio, USB and any network depend on the carrier board | A device tree per carrier board, if its wiring differs. |
| Zero 2 W | Probably boots | Wi-Fi (CYW43436), the board's only network | Unknown without hardware. If the 43436 reports chip ID 43430 with a different revision, it may need only its own firmware and NVRAM files and a revision check. Not confirmed. |

## Pi 3 Model B

What works on the target board. Boot-args go in `uEnv.txt` on the boot
partition as `extra_bootargs=...` (`tools/make_realhw_sd_image.sh` writes
`cpus=4 wifi=1 bt=1` there by default).

### Works

| Hardware | Status | How it is enabled | Notes and docs |
|---|---|---|---|
| CPU, 4 × Cortex-A53, 64-bit, 1.2 GHz | Works | `cpus=4` (the real-hardware image's default). Without it, one core. | All four cores start, take IPIs and run user threads. Verified 2026-10-02. `sdcard/config.txt` sets `force_turbo=1`: nothing in the port asks the firmware for a higher clock, so without it the cores stay at 600 MHz. `docs/smp-bringup.md` |
| Memory | Works | Always | 1 GB, of which `gpu_mem=128` in `config.txt` gives the VideoCore 128 MB (its H.264 decoder needs that for 1080p). XNU gets the remaining 896 MB, as reported by the firmware through U-Boot. |
| Boot (firmware → U-Boot → XNU) | Works | Always | `tools/make_realhw_sd_image.sh` builds the card image. |
| SD card | Works | Always | On the sdhost controller with `wifi=1` (the default), which leaves the Arasan SDHCI controller to drive the Wi-Fi chip over SDIO. Without `wifi=1` the card is on SDHCI. The root file system is HFS+. |
| USB host (DWC2) and hubs | Works | Always | Includes the LAN9514's built-in hub. `usb-apple-hub=1` uses Apple's hub driver for hot-plug instead. |
| USB keyboard and mouse | Works | Always | Through IOHIDFamily. The desktop, the text console and X all take input. |
| Ethernet (LAN9514, 10/100) | Works | Always | `en0`, with a DHCP lease from IPConfiguration (configd). |
| Wi-Fi (CYW43438 / BCM43430, 802.11 b/g/n, 2.4 GHz) | Works | `wifi=1` (in the real-hardware image's default boot-args) | `en1`. Scans and joins WPA2-PSK networks through the firmware supplicant. Gets a DHCP lease at boot. About 1 MB/s down and 0.7 MB/s up. The Naples menu bar and Settings both have a Wi-Fi panel. `docs/wifi-plan.md` |
| Bluetooth (BCM43430A1, HCI over PL011) | Works, partly | `bt=1` (in the default boot-args) | The controller comes up at 921600 baud with its patch (shipped in the image, with its licence, in `/usr/share/firmware/bluetooth/`). The controller is verified on the real Pi (2026-10-12). The host stack on top of it (NetBSD's netbt in the kernel, pairing, HID keyboards and mice) works on QEMU and has not yet run on the Pi; pairing is done from the Bluetooth menu in the Naples menu bar or Settings' Bluetooth pane. No audio or BLE yet. `docs/bluetooth-plan.md`, `docs/bluetooth-stack-plan.md` |
| HDMI display | Works | Always | The framebuffer the firmware sets up, at the mode `config.txt` picks. Carries the text console and the X server (Xorg with the `puredarwingop` driver). `docs/framebuffer-mapping.md` |
| 3D GPU (VideoCore IV V3D) | Works | Always | OpenGL 2.1 / OpenGL ES 2.0 through Mesa. Florence's web rendering and Venice's video use it. `docs/v3d-bringup.md`, `docs/gpu-opengl-plan.md` |
| Hardware video decode (H.264, MPEG-4) | Works | Always (`vchiq=0` turns it off) | On the VideoCore through VCHIQ and MMAL. Venice uses it through FFmpeg's mmal decoder. Known issue: the last frames before end-of-stream are dropped. `docs/video-decode.md` |
| Audio out, 3.5 mm jack | Works | On by default (`audio=0` turns it off) | PWM fed by DMA, as an IOAudioFamily device, through `audiod`. `docs/audio-bringup.md` |
| Serial console (UART) | Works | `serial=3` | The kernel log and a login shell on the GPIO header's UART pins. |

### Not supported

| Hardware | Status | Notes |
|---|---|---|
| HDMI audio | Not supported | A separate effort from the 3.5 mm jack (DARW-311), not started. |
| USB mass storage (flash drives, disks) | Not supported | No USB mass-storage driver is built. |
| Bluetooth headphones and speakers, BLE devices | Not supported | Classic keyboards and mice are in the Bluetooth row above. Audio needs A2DP (step S6 of `docs/bluetooth-stack-plan.md`), and BLE (S8) needs an LE host. |
| GPIO header pins for user programs, I2C, SPI | Not supported | No drivers. The kernel uses GPIO pins only for its own devices (SD, audio, Wi-Fi, Bluetooth). |
| Camera (CSI) | Not supported | No driver. |
| DSI display (the official touchscreen) | Not supported | No driver. |
| Hardware video encode | Not supported | Only decode is wired up. |
| CPU frequency scaling, thermal management | Not supported | No driver asks the firmware for clock changes. The cores run at a fixed 1.2 GHz (`force_turbo=1`); the firmware's own thermal throttling still applies. |
| Changing the display mode at run time | Not supported | The mode is fixed at boot by the firmware (`config.txt`). |

## QEMU

Development mostly happens on QEMU's `-M raspi3` machine, using this project's
fork `third_party/qemu-zhuowei` (see `CLAUDE.md`). QEMU emulates the Pi 3's
SoC but not all of its board.

| Hardware | In QEMU |
|---|---|
| CPU, SD card, USB host, USB keyboard, serial console | Emulated, used by every boot test (`tools/boot_test.py`). |
| Ethernet | QEMU has never emulated the LAN9514. Tests use QEMU's `usb-net` device instead. |
| Wi-Fi, Bluetooth | Not emulated. Bluetooth's host stack is tested against `tools/bt_h4_responder.py`, which plays the controller (and a keyboard) on the Pi's UART: `tools/bt_netbt_qemu_test.sh`, `bt_hid_qemu_test.sh`, `bt_pair_qemu_test.sh`, `bt_ssp_qemu_test.sh`. Wi-Fi and the real controller's patch load are real hardware only. |
| HDMI framebuffer | Shown by `./runvm_hdmi.sh`, but X does not draw correctly on it (DAR-443). GUI tests run X on Xvfb instead, for example `tools/darwin_util_screenshots_qemu.sh`. |
| V3D GPU, video decode | Not emulated. OpenGL falls back to Mesa's softpipe; video decode is real hardware only. |
| Audio | Simulated with `pwm-sim=1`, without sound. |
