# WiFi on the Pi 3 (BCM43438 over SDIO): plan

Status (2026-10-12): W0-W4 implemented and working on a real Pi with
boot-arg wifi=1:
- the root disk runs on sdhost; the write self-test passes on every boot
  with `sdhost-write-test=1`;
- the firmware boots and scans;
- WPA2-PSK joins through the firmware supplicant;
- en1 gets a DHCP lease from `org.puredarwin.wifi` at boot;
- RSSI and Opkts report real values;
- bulk scp works in both directions with en0 down (8 MB, md5 matches;
  about 0.7 MB/s up and 1 MB/s down).

The earlier boot failure was sdhost corrupting the tail of every
multi-block write: CMD12 went out before the FIFO drained, which damaged
the kernel file and the HFS catalog. It is fixed by `waitWriteDrained()`,
and the self-test guards it when booted with `sdhost-write-test=1`.

Code review (2026-10-12) found 8 bugs, all fixed:
- sdhost lost data errors latched after the last word;
- sdhost ignored R1 card-status errors;
- sdhost had one 500 ms deadline for a whole 128 KB chunk;
- a firmware scan record could wrap the IE bounds check;
- control frames ignored the SDPCM credit window;
- GPFSEL read-modify-write raced between sdhost and PWM;
- the EROM walk read past its buffer;
- the join input could be missing its NUL.

After the fixes, all of these still pass on the Pi: the self-test, WiFi
autostart, the bulk scp both ways with en0 down, and 40 `wifi status`
ioctls during bulk TX.

Plan written 2026-10-11. Target board: Raspberry Pi 3 Model B (LAN9514
Ethernet, `0424:ec00`, so the radio is the CYW43438 / BCM43430A1, 2.4 GHz
b/g/n, not the 3B+'s 43455). Real hardware only for the radio itself: QEMU
emulates no SDIO WiFi chip.

## References (read before writing code, not after)

| What | Where | Why |
|---|---|---|
| Plan 9 `ether4330.c` + `emmc.c` (R. Miller), as carried by Circle `addon/wlan/` | github.com/rsta2/circle | Smallest complete BCM43430 SDIO FullMAC driver: SDIO init, backplane, core scan, firmware/NVRAM upload, SDPCM, BCDC ioctls, events, join. Primary model for the chip side. Plan 9 licence is MIT since 2021. |
| Linux `brcmfmac` (`sdio.c`, `bcmsdh.c`, `chip.c`, `fwil.c`, `bcdc.c`, `cfg80211.c`, `feature.c`) | raspberrypi/linux `drivers/net/wireless/broadcom/brcm80211/brcmfmac` | Authority for details ether4330 skips: 43430 SR/KSO, `clmload`, firmware supplicant (`sup_wpa`, `wsec_pmk`), escan layout, event mask. |
| Linux `bcm2835.c` (sdhost) + Circle `addon/SDCard/sdhost.cpp` | as above | The SD-card controller this plan moves the root disk to. |
| QEMU `hw/sd/bcm2835_sdhost.c`, `hw/gpio/bcm2835_gpio.c` | third_party/qemu-zhuowei | QEMU has the sdhost model, and the GPIO model moves the card between sdhci and sdhost on GPFSEL48-53 writes: all six ALT0 -> sdhost, all six INPUT -> sdhci; ALT3 changes nothing (`gpfsel_set()`). The sdhost driver is QEMU-testable for protocol and data, not for clock, CRC or busy timing (the model discards SDCDIV and completes every command instantly). |
| Linux DT `bcm2710-rpi-3-b.dts`, `bcm2837-rpi-3-b.dts`, `bcm283x.dtsi` | raspberrypi/linux | Pin routing, power GPIOs. |
| Firmware | RPi-Distro/firmware-nonfree (`cypress/cyfmac43430-sdio.bin` 7.45.98, `.clm_blob`, `brcm/brcmfmac43430-sdio.txt`) | What Raspberry Pi OS ships for this board (`brcmfmac43430-sdio.raspberrypi,3-model-b.*` symlink to these). |

## Hardware facts this plan rests on

1. **The WiFi chip is wired to GPIO34-39, and only the Arasan controller
   ("sdhci", `0x3f300000`, IRQ 62) reaches those pins (ALT3).** sdhost
   (`0x3f202000`, IRQ 56) reaches only GPIO48-53 (ALT0). (bcm283x.dtsi:
   `emmc_gpio34` = ALT3, `sdhost_gpio48` = ALT0; no sdhost group on 34-39.)
2. Today `BCM2837SDHCI` drives the **SD card** from the Arasan controller
   (GPIO48-53 ALT3) and deliberately parks GPIO34-39 as inputs
   (`pinmuxSDSlot()`). So WiFi needs the SD card moved to sdhost first.
   This is exactly Linux's and Circle's Pi 3 layout (Circle warns "WLAN
   cannot be used parallel with SD card access" without `USE_SDHOST`).
3. Power: WL_ON is firmware expander GPIO 1 (`wifi_pwrseq reset-gpios =
   <&expgpio 1 GPIO_ACTIVE_LOW>`), driven through the mailbox property tag
   `SET_GPIO_STATE` (0x00038041) with gpio = 128 + 1. BT_ON is expander
   GPIO 0. Circle never touches WL_ON on Pi 3 and still works, so the
   firmware leaves it on; we power-cycle it only as a recovery step.
4. The 32 kHz LPO for the 43438 is GPCLK2 on GPIO43 (ALT0) in Linux's DT.
   The firmware sets it up; the driver logs GPFSEL4 bits for GPIO43 on the
   first run instead of touching it.
5. Console is the mini UART (GPIO14/15, `core_freq=250`), so nothing here
   touches it. PL011 stays free for Bluetooth (separate plan).
6. Firmware has in-dongle supplicant: the image's version string is
   `43430a1-roml/sdio-g-pool-p2p-idsup-idauth-...` (`idsup`). So WPA2-PSK
   can run on the chip (Linux `BRCMF_FEAT_FWSUP`: `sup_wpa=1` +
   a PMK via ioctl 268). No host supplicant is needed for WPA2-PSK.

## Architecture

```
  SD card (root disk)                       BCM43438 WiFi
       |  GPIO48-53 ALT0                         |  GPIO34-39 ALT3
  BCM2837SDHost : IOBlockStorageDevice     BCM2837SDHCI (Arasan) in SDIO mode
       |   (new, DT node "sdhost")              |   publishes nub
  IOBlockStorageDriver -> IOMedia ...      BCM2837SDIODevice : IOService
                                                |   (CMD52/CMD53, card IRQ)
                                           BCM43430Wifi : IOEthernetController  -> en1
                                                |   (SDPCM/BCDC, firmware, events)
                                           BCM43430WifiUserClient  <- /usr/sbin/wifi
```

Role selection is one boot-arg, `wifi=1` (uEnv.txt `extra_bootargs`):

- absent / `wifi=0`: today's behaviour, byte for byte. sdhost driver
  declines; Arasan drives the SD card.
- `wifi=1`: `BCM2837SDHost` owns the SD card (pinmux GPIO48-53 ALT0);
  `BCM2837SDHCI` waits for sdhost's verdict and then runs in SDIO mode on
  GPIO34-39.

**Automatic fallback:** card ownership is one compare-and-swap
(`iokit/BCM2837ArmIO/BCM2837SDCardOwner.h`), taken before either driver
writes a GPIO48-53 function bit. `BCM2837SDHost::start()` claims the card,
and publishes the resource `BCM2837SDCardHost` = `"sdhost"` on success or
`"sdhci"` when card init fails (then pins go INPUT, then ALT3, and
ownership is handed back). `BCM2837SDHCI` with `wifi=1` waits for that
verdict (15 s); on timeout it claims the card itself, and only if sdhost
has not already claimed it (then it keeps waiting). It takes the SDIO
role only when the card is on sdhost. A bad sdhost driver therefore costs
WiFi, not the root disk. Data corruption is not covered by this; that is
what the QEMU write tests and the R/W soak in step W1 are for.

The default flips to `wifi=1` only after W1-W4 pass on the real Pi.

## Steps

Each step ends with a QEMU or real-hardware check named below, and an
adversarial review before its commit.

### W0. Shared plumbing (small)

- `BCM2837Mailbox::setFirmwareGPIO(unsigned expPin, bool on)` /
  `getFirmwareGPIO()` (tags 0x00038041/0x00030041, pin + 128). Log the
  current WL_ON/BT_ON state at start.
- `BCM2837GPIO` helper (header-only static functions, physical
  `0x3f200000`): `setFunction(pin, fsel)`, `setPull(pin, off|up|down)`
  using the GPPUD/GPPUDCLK sequence (150-cycle waits, BCM2835 ARM
  Peripherals 6.1). Replace the open-coded GPFSEL writes in
  `BCM2837SDHCI::pinmuxSDSlot()` with it (same values; QEMU boot check).
- DT: add `sdhost { reg = <0x202000 0 0x100 0>; interrupts = <56>; }`
  under `arm-io` in `rpi3.dts` (all variants), personality
  `BCM2837SDHost` matching name "sdhost".

### W1. BCM2837SDHost: SD card on the sdhost controller

New `iokit/BCM2837SDHost/` (`IOBlockStorageDevice`, same 13 methods and
same `doAsyncReadWrite` synchronous-under-a-mutex shape as
`BCM2837SDHCI`, so the IOKit/storage side is already proven).

- Register sequence from Linux `bcm2835.c` (`bcm2835_reset_internal`,
  `bcm2835_set_clock`, `bcm2835_send_command`, `bcm2835_transfer_pio`,
  `bcm2835_finish_command`): SDVDD power cycle, SDHCFG/SDEDM thresholds,
  SDCDIV = core clock / f - 2 (core clock from the mailbox, clock id 4
  CORE; 250 MHz pinned by `core_freq=250`), 400 kHz identify then 25 MHz.
- Card protocol: same commands the SDHCI driver already sends (CMD0, CMD8,
  ACMD41, CMD2, CMD3, CMD9, CMD7, ACMD51/ACMD6 4-bit, CMD16, CMD17/18,
  CMD24/25, CMD12). Duplicated from `BCM2837SDHCI` rather than refactored
  into a shared layer now, so the working root-disk driver is not touched;
  a follow-up ticket can merge them once both are proven.
- PIO through SDDATA, 16-word FIFO, SDHBCT/SDHBLC set per transfer;
  SDEDM FSM checks after writes (Linux's `bcm2835_check_data_error`
  / busy wait on `SDCMD_BUSYWAIT` for CMD12/CMD24/25 like Linux).
- No command-complete interrupt exists on sdhost (Linux spin-polls
  SDCMD NEW_FLAG, `bcm2835_read_wait_sdcmd`). The driver polls
  everything: NEW_FLAG with short spins, data via the SDEDM FIFO count,
  busy via SDHSTS_BUSY_IRPT, card programming via CMD13. IRQ 56 is in the
  DT for later use.
- Clock: SLOW_CARD set in SDHCFG, so the 11-bit ident divider is used in
  data mode too (the data-mode divider has only 3 bits: 250/(8+2) would
  read as 125 MHz). The firmware tag 0x00038042 SET_SDHOST_CLOCK is the
  alternative Linux and Circle use; not needed while core_freq is pinned.
- SDHBCT before SDHBLC (QEMU arms on the SDHBLC write); SDHBLC is 9 bits,
  so requests are split into 256-block commands.
- Pinmux at start: GPIO48-53 -> ALT0, pull-ups on CMD/DAT0-3 (Linux
  `sdhost_gpio48`), then the fallback protocol above.

Checks:
- QEMU with `wifi=1`: HFS root mounts from sdhost, `ls`, write a file,
  `sync`, reboot, file is there, `fsck_hfs -n` clean. Boot without the arg
  still uses sdhci (regression check).
- QEMU fallback: boot with `wifi=1 sdhost-fail-test=1` (driver pretends
  card init failed): root still mounts via sdhci.
- Real Pi with `wifi=1`: root on sdhost, then a 50 MB write + read-back
  checksum + `fsck_hfs -n`. WiFi not yet involved.

### W2. Arasan SDIO host mode

`BCM2837SDHCI` with `wifi=1` and verdict "sdhost":

- Pinmux GPIO34-39 ALT3, pulls: CLK off, CMD/DAT up (bcm283x.dtsi
  `emmc_gpio34`). Leave GPIO48-53 alone.
- Reuse the existing reset, clock (`setClock`, divider header), register
  write spacing and the DAR-316 combined TRANSFER_MODE/COMMAND write. No
  ACMD/CSD identify; instead publish a `BCM2837SDIODevice` nub and do not
  `registerService()` the block device (so no IOBlockStorageDriver
  attaches).
- Nub API (all blocking, serialized by the existing transfer mutex):
  `sendCommand(idx, arg, respType, resp)`, `ioRWDirect(write, fn, addr,
  val, &out)` (CMD52), `ioRWExtended(write, fn, addr, incr, buf, len,
  blockSize)` (CMD53 byte mode for < blockSize, block mode otherwise,
  max 511 blocks), `setBusWidth4()`, `setClockHz()`, and
  `enableCardInterrupt(handler, target)` (SDHCI CARD_INT, INT bit 8; the
  host masks it in `interruptOccurred()` until the client re-arms it, as
  SDHCI requires because CARD_INT is level-triggered).
- Data phase for CMD53 uses the same PIO loop as `readBlocksPIO`/
  `writeBlocksPIO`, with block size 64 (F1) or 512 (F2), and byte-mode
  transfers of 1-512 bytes.

Check (real Pi, `wifi=1`, no WiFi driver yet: boot-arg `sdio-probe=1`
makes the host run the SDIO identify itself): CMD0, CMD5 OCR read, CMD3,
CMD7, CCCR read (`0x00` CCCR/SDIO rev, Fn0 0x08 capability) logged, chip
id read through the backplane window (expect 43430 = 0xa9a6, rev 1).
This is the first moment the radio talks.

### W3. BCM43430Wifi driver (chip bring-up, no networking yet)

New `iokit/BCM43430Wifi/`, matches `BCM2837SDIODevice`. Runs all SDIO
traffic on one dedicated kernel thread (`kernel_thread_start`), because
every SDIO access blocks; IOKit-facing calls hand work to it through a
command queue + IOLock sleep/wakeup.

Port of ether4330's `sdioinit` -> `sbinit` -> `fwload` -> `sbenable`, with
brcmfmac's 43430 specifics:

1. Fn0: 4-bit bus (CCCR 0x07), high-speed off at first (25 MHz), F1 block
   size 64, F2 512, enable F1, wait IORDY.
2. Backplane: window via 0x1000a-c, chip id at 0x18000000, EROM core
   scan (chipcommon, ARM CM3 0x82a, SOCRAM 0x80e, SDIO 0x829, D11 0x812).
3. Halt ARM, reset D11 and SOCRAM, size RAM (banks), 43430 bank-3 PDA
   write (`ramscan`), ALP clock.
4. Upload firmware to RAM 0, read back and compare (ether4330
   `Firmwarecmp`), upload condensed NVRAM to the top of RAM, write the
   length token, release ARM.
5. HT clock request, protocol version mailbox (4 << 16), interrupt mask
   (FrameInt | MailboxInt | Fcchange), enable F2, enable Fn0 interrupts.
6. SDPCM receive loop: card IRQ (or 10 ms poll) -> read Intstatus, ack,
   read frames from F2 (len/~len check, rx halt on error as ether4330
   does), dispatch by channel: 0 control, 1 event, 2 data. Track the
   credit window (`window`, `fcmask`) for transmit.
7. BCDC ioctl/iovar layer (`wlcmd`, `wlgetvar`, `wlsetvar`) with request
   ids and a 2 s timeout.
8. Init iovars as brcmfmac/ether4330 do: `bus:txglom` 0, `bus:rxglom` 0,
   `clmload` from the CLM blob, `country` "XX"/configurable,
   `cur_etheraddr` read (OTP MAC, also used as en1 MAC), `event_msgs`
   mask (SET_SSID, JOIN, AUTH, ASSOC, REASSOC, DEAUTH(_IND),
   DISASSOC(_IND), LINK, PSK_SUP, ESCAN_RESULT), `mpc` 0, WLC_UP (2).
9. Log firmware version (`ver` iovar), `cap` iovar (expect `idsup`).

Firmware delivery: **userland pushes the blobs** through the user client
(below), from `/usr/share/firmware/wifi/` on the root disk. Reasons: no
kernel rebuild to change firmware, no Broadcom binaries committed into
our xnu fork (sync_iokit_drivers.sh commits into it), and it is how Linux
loads it too. `tools/userland_staging/fetch_wifi_firmware.sh` downloads
the three files from firmware-nonfree at a pinned commit, checks
SHA-256s, and `inject_into_sd_image.sh` installs them. They are
gitignored.

Check (real Pi): `wifi up` loads firmware; kernel log shows chip id,
RAM size, "firmware ready" mailbox, `ver` string 7.45.98, and the MAC.

### W4. Network interface + scan/join

- `BCM43430Wifi : IOEthernetController`, attaches `IOEthernetInterface`
  (en1) once the firmware is up. MAC = `cur_etheraddr`.
- TX: `outputPacket()` enqueues the mbuf; the SDIO thread copies it into
  an SDPCM frame (channel 2, BDC header `0x20`, 4-byte aligned) when the
  credit window allows, else returns `kIOReturnOutputStall` and restarts
  the queue on a window update (same idea as ether4330's `txstart`/
  `flowstart`).
- RX: channel 2 frames, strip SDPCM + BDC (`4 + data_offset*4`), copy into
  an mbuf, `inputPacket()`. Promiscuous/multicast: `mcast_list`,
  `allmulti` iovars.
- Link: E_LINK flags bit 0 and E_PSK_SUP status 6 (keyed) drive
  `setLinkStatus(kIONetworkLinkValid|Active)`; deauth/disassoc/link-down
  clear it.
- Scan: `escan` iovar (version 1, action start, sync id), collect
  E_ESCAN_RESULT BSS info (SSID, BSSID, channel, RSSI, capability, RSN/WPA
  IEs), dedupe by BSSID, finish on status 0.
- Join: open (`wsec` 0, `wpa_auth` 0) and WPA2-PSK (`wsec` 4 AES,
  `wpa_auth` 0x80, `sup_wpa` 1, `wsec_pmk` {len, flags=1 passphrase,
  key}, `infra` 1, `auth` 0, then `WLC_SET_SSID` 26 with the SSID). Join
  result from E_SET_SSID status; WPA2 done when E_PSK_SUP reports keyed.
  WPA/TKIP and WPA3 are out of scope.
- User client `BCM43430WifiUserClient` (external methods): load firmware
  chunk (type, offset, data <= 4 KB), boot, scan start, scan results
  (struct out), join (ssid, passphrase), leave, status (state, SSID,
  BSSID, channel, RSSI via `WLC_GET_RSSI` 127). Root only (check
  `clientHasPrivilege(kIOClientPrivilegeAdministrator)`).

Userland:
- `tools/userland_staging/wifi.c` + `build_wifi.sh` (dynamic, IOKit, the
  `audio_play` pattern; runs `bind_target_audit.sh`):
  `wifi up | scan | join <ssid> [passphrase] | leave | status`.
- `/etc/wifi.conf` (`ssid=`, `psk=`, mode 600) and a launchd job
  `org.puredarwin.wifi` (plist naming as the other jobs in `tools/userland_staging/`) running `wifi up && wifi join -f /etc/wifi.conf`
  at boot when the file exists; DHCP through `ipconfig set en1 DHCP`
  (check first whether IPConfiguration already starts DHCP on a new
  interface the way it does for en0).

Checks (real Pi):
- `wifi scan` lists the local networks with plausible RSSI/channel.
- `wifi join <ssid> <psk>`, `ipconfig set en1 DHCP`, en1 gets a lease;
  ping the router and an internet host; ssh to the Pi over WiFi with the
  Ethernet cable unplugged; 20 MB scp both ways with checksum.
- Wrong passphrase: join fails cleanly with a message, no hang.
- Reboot with `/etc/wifi.conf`: en1 comes up unattended.

## Risks / unknowns

- **Root disk move.** Biggest risk in the plan. Mitigations: off by
  default, automatic fallback on init failure, QEMU write/fsck tests,
  real-HW soak before WiFi. Recovery from a bad kernel is the SD card in
  the Mac (delete the `wifi=1` line in uEnv.txt).
- Arasan + SDIO quirks. The DAR-316 register-write issues apply equally
  to CMD52/53. CMD53 byte-mode block sizes not multiple of 4 must be
  handled in PIO (pad to 4 like ether4330 `packetrw`).
- 43430 SR/KSO (save-restore, keep-SDIO-on). brcmfmac enables KSO for
  chips with SR; ether4330 does not and works on Pi 3 with this firmware.
  Start without; if the chip stops answering after idle, add
  `brcmf_sdio_kso_control` from brcmfmac.
- Firmware supplicant: if `sup_wpa` set fails at runtime, fall back to a
  host 4-way handshake (PBKDF2/PRF-SHA1/AES key wrap) in the userland
  tool via EAPOL frames - a separate step, only if needed.
- Throughput is PIO-bound; fine for ssh/DHCP/NTP. DMA is later work.
- 128 MB RAM on the real Pi (`RPI3_DRAM_SIZE`): driver buffers stay
  small. Firmware chunks are written to chip RAM as they arrive (ARM held
  in reset), so the kernel never holds the 400 KB image; only the NVRAM
  (1 KB, condensed at boot) and CLM blob (5 KB, sent after boot) are kept.

## Out of scope

AP mode, P2P, 5 GHz (chip has none), WPA3/SAE, enterprise (802.1X),
roaming between BSSIDs, power save tuning, a CoreWLAN/Airport API.

## Changes after review (2026-10-11)

An adversarial review of this plan (against the code, Linux, Circle and
QEMU sources) changed the following. Where this section and the text
above disagree, this section wins.

- **W1** as above: ownership CAS, INPUT-then-ALT3 hand-back, polled
  driver, SLOW_CARD, 9-bit SDHBLC. Extra QEMU check: with `wifi=1` the
  Arasan bus is empty in QEMU, so the SDIO path must fail fast (CMD5
  timeout) without delaying root mount.
- **W2**: drop `sdio-probe=1`; W3 steps 1-2 are the W2 check. The CMD53
  nub splits a transfer into a block-mode part plus a byte-mode
  remainder (ether4330 `sdiorwext`); a byte count of 512 encodes as 0.
  The CARD_INT callback runs at primary interrupt level and only wakes
  the SDIO thread.
- **W3**:
  - Keep ForceHT set after HT is available and set `WLC_SET_PM` (86) = 0
    (this firmware is built with save/restore, `srfast`; ether4330 avoids
    KSO only by doing both). Write `SBSDIO_WATERMARK` (0x10008) = 8 after
    F2 is enabled (brcmfmac default).
  - `infra` is ioctl `WLC_SET_INFRA` (20). Also `roam_off` 1,
    `bcn_timeout`, `assoc_listen` as ether4330 does.
  - `country` takes `{char abbrev[4]; le32 rev; char ccode[4]}`; use
    rev 0 and ccode = abbrev.
  - `clmload` is mandatory (firmware is `noclminc`): chunks of at most
    1400 bytes with DL_BEGIN/DL_END flags, then check `clmload_status`.
  - Events are big-endian (`brcmf_event_msg_be`).
  - escan: query `scan_ver`, fall back to v1.
  - RX reads the 12-byte SDPCM header first, then the rest.
  - Add a user-client "reset upload" method so an interrupted `wifi up`
    leaves no half-loaded chip.
- **W4**:
  - WPA2-PSK: `wsec` 4, `wpa_auth` 0x80, `sup_wpa` 1, then the PMK
    through **ioctl 268 `WLC_SET_WSEC_PMK`** (not an iovar) as the 7.45-era
    68-byte struct `{u16 key_len; u16 flags; u8 key[64]}` with flags = 1
    (passphrase) and key_len = strlen, then `WLC_SET_SSID`
    (brcmfmac `cfg80211.c` connect path). Fallback if the firmware
    rejects the passphrase form: userland PBKDF2-HMAC-SHA1(psk, ssid,
    4096, 32), sent with flags 0 and key_len 32. No host 4-way handshake.
  - TX: `createOutputQueue()` returns an `IOBasicOutputQueue` (precedent
    `AppleUSBCDCECMData.cpp`); `outputPacket()` copies into a bounded
    ring and returns Stall only when the ring is full; the SDIO thread
    calls `service()` when the ring or the credit window opens.
  - Threading: `inputPacket()` only from the SDIO thread; `setLinkStatus`
    from that thread is fine (medium lock only); that thread never takes
    the workloop gate while gated calls may be waiting on it.
  - DHCP: add an en1 DHCP service to the SCPreferences plist written by
    `inject_into_sd_image.sh` (plus migration for existing images), so
    IPConfiguration starts DHCP on link-up. en1's BSD name comes through
    `BCM2837DWC2`'s IONetworkStack notifier, so WiFi depends on USB
    having started.
  - User client precedent is `BCM2837V3DUserClient` (`clientHasPrivilege`,
    `externalMethod`), not audio_play's entitlement scheme.
