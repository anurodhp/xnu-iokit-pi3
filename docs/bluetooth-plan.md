# Bluetooth on the Raspberry Pi 3 (CYW43438 / BCM43430A1)

Status (2026-10-12): B1-B5 implemented, reviewed, and working on a real
Pi 3B with boot-arg bt=1. `org.puredarwin.bluetooth` runs `bt up` at boot.
- The patch loads (121 records), the link switches to 921600, and the
  controller reports BD_ADDR b8:27:eb:12:61:de and the name "BCM43438A1
  37.4MHz Raspberry Pi 3-0141".
- `bt scan 15` found 24 devices (23 LE, 1 classic with its name).
- The overrun, framing, ring-drop and resync counters stayed at 0.
- An 8 MB WiFi scp running during the scan arrived intact (24.5 s instead
  of 16 s).
- QEMU: the whole path passes against `tools/bt_h4_responder.py`.

Hardware finding: the firmware leaves the per-tag response bit at 0 for
the expander GPIO tags. Only the message code and the status word are
meaningful, as Linux treats them (`raspberrypi.c:121`).

Code review found 5 bugs, all fixed: a short BD_ADDR reply reported
success; user commands could interleave with `up`; a flush could move the
ring tail backwards; `bt hci` printed stack on failure; a `start()` failure
left the interrupt registered.

Open: L2CAP and profiles (out of scope); a long-duration overrun stress
test. Companion to `docs/wifi-plan.md`;
the WiFi half of the same chip is done and verified on hardware.

## What the hardware is

The CYW43438 combo chip carries the Bluetooth controller next to the WiFi
core. WiFi is on SDIO; Bluetooth is a separate HCI-over-UART (H4) link.

| Item | Pi 3 Model B | Source |
|---|---|---|
| UART | PL011 "uart0", ARM phys `0x3f201000`, GPU IRQ 57 (bank 2 bit 25; vector 32+25 in BCM2837InterruptController.h:67-69). UARTCLK 48 MHz on the Pi (Circle `machineinfo.cpp:488-490`) | Linux `bcm283x.dtsi` (`uart0`), `bcm2837-rpi-3-b.dts` ("uart0 communicates with the BT module") |
| Pins | GPIO32 TXD0 / GPIO33 RXD0, ALT3; pulls OFF / UP | `bcm283x.dtsi` `uart0_gpio32` |
| Flow control | none on the 3B: its `&uart0` pinctrl is only `uart0_gpio32` + `gpclk2_gpio43`. `uart0_ctsrts_gpio30` (GPIO30/31) exists in the dtsi but the 3B does not use it | `bcm2837-rpi-3-b.dts:131-135` |
| Power | `BT_ON` = firmware expander GPIO 0, reached through mailbox tag `SET_GPIO_STATE` `0x00038041` with gpio = 128 + 0 | `bcm2837-rpi-3-b.dts:25-37`, `gpio-raspberrypi-exp.c` (`RPI_EXP_GPIO_BASE 128`), `raspberrypi-firmware.h:88` |
| LPO clock | 32 kHz on GPIO43 GPCLK2, already set up by the VideoCore firmware (seen on hardware: "GPIO43 function 4 (GPCLK2)") and shared with WiFi | `gpclk2_gpio43`, BCM2837SDHCI boot log |
| Firmware | `BCM43430A1.hcd`, RPi-Distro/bluez-firmware commit `cdf61dc691a49ff01a124752bd04194907f0f9cd`, `debian/firmware/broadcom/BCM43430A1.hcd`, 30049 bytes, SHA-256 `c096ad4a5c3f06ed7d69eba246bf89ada9acba64a5b6f51b1e9c12f99bb1e1a7` | |
| Speeds | 115200 at power-up. Upstream says 2000000 (`bcm283x-rpi-wifi-bt.dtsi` `max-speed`), but the downstream Raspberry Pi kernel overrides the 3B (no flow control) to 921600 (`bcm2710-rpi-3-b.dts:202-204`). Default here: 921600 | `hci_bcm.c:1303` (`init_speed = 115200`) |

The console is the mini-UART on GPIO14/15 (`sdcard/config.txt`,
`pe_serial.c`), so the PL011 is free. Nothing in this port touches it today.

## What a "Bluetooth stack" can mean here

Apple never open-sourced IOBluetoothFamily or blued (`git ls-remote` of
`apple-oss-distributions/IOBluetoothFamily`: repository not found, checked
2026-10-12). The source-priority fallback would be FreeBSD's netgraph
Bluetooth stack (`ng_hci`, `ng_l2cap`, `ng_btsocket`, `hccontrol`, `sdpd`),
which needs netgraph itself: a large port of its own. So this plan stops at
the HCI boundary, and that boundary is designed so a real stack can sit
on it later:

- **In scope:** power, UART transport, firmware (patchram) load, baud switch,
  and a raw HCI channel to userland. A `bt` tool on top of it: `up`, `info`
  (BD_ADDR, HCI/LMP version, local name), `scan` (classic inquiry plus LE
  passive scan with names and RSSI), and `hci <opcode> [bytes]` for anything
  else.
- **Out of scope (later phase, own plan):** L2CAP, SDP, RFCOMM, pairing and
  security manager, HID and audio profiles. The likely route is the FreeBSD
  netgraph stack on top of the raw channel, or a userland stack (BTstack-style)
  using the same user client.

## Design

### B1. Expander GPIO and pinmux

- `BCM2837Mailbox::setExpGPIO(unsigned n, bool on)` via the existing
  `propertyTag2(0x00038041, 128 + n, on)`. The first response word is a
  status, and 0 means success (`gpio-raspberrypi-exp.c:191`,
  `if (ret || set.gpio != 0)`).
- `BCM2837GPIO::setFunction(32, 2, kFSelAlt3)`, `setPull(32, 1, kPullOff)`,
  `setPull(33, 1, kPullUp)`. These run under the helper's spinlock, which
  was added for the WiFi review: GPIO32/33 share GPFSEL3 with the SDIO pins
  34-39. Mux only at `up` time.
- Power cycle: BT_ON low, then high, then wait 120 ms. Linux's power-on
  path sleeps 100-120 ms (`bcm_gpio_set_power`, `hci_bcm.c:301`); the 15 ms
  at :788/:807 is suspend/resume. Do not touch WL_ON (expander 1).

### B2. `BCM43430Bluetooth` HCI-UART transport (kernel)

A new driver, `iokit/BCM43430Bluetooth/BCM43430Bluetooth.{h,cpp}`. It
matches a new DT node named `bt-uart`, not `uart0`, because `pe_serial.c:823`
falls back to a node called `uart0` as the console. The node has
`reg = <0x201000 0 0x1000 0>`, `interrupt-parent = <0x1>` and
`interrupts = <57>`. There is a personality in `BCM2837Personalities.cpp`
and `iokit/conf/files` entries in the xnu fork. It only runs with boot-arg
`bt=1`, so the default boot does not change.

- PL011 registers from Linux `include/linux/amba/serial.h` (DR 0x00, FR 0x18,
  IBRD 0x24, FBRD 0x28, LCRH 0x2c, CR 0x30, IFLS 0x34, IMSC 0x38, MIS 0x40,
  ICR 0x44). UARTCLK comes from mailbox clock id 2 (UART); the divisor is
  `UARTCLK*4/baud` split into IBRD/FBRD (`amba-pl011.c`, `pl011_set_termios`).
  8N1, FIFOs on.
- **RX is interrupt driven.** There is no flow control on the 3B, and at
  921600 the 16-byte RX FIFO fills in about 174 µs. Polling from a thread
  would drop bytes.
  - The handler runs at primary interrupt level with RXIM + RTIM and IFLS
    RX 1/2. It loops on MIS until it reads zero (`pl011_int`) and drains
    the FIFO into a 64 KB single-producer/single-consumer ring. It counts
    OE/FE/BE/PE.
  - RXIS/RTIS are cleared by draining, never by writing ICR. Linux masks
    them out of the ICR write (`amba-pl011.c:1640`), and QEMU raises RX only
    on the transition to the trigger level (`pl011.c:257`), so an ICR clear
    after the drain strands bytes.
  - The worker sleeps with a 10 ms timeout, so a wakeup lost from the
    unlocked `IOLockWakeup` at interrupt level costs latency, not data.
  - Interrupts are masked for a whole kprintf line (`pe_kprintf.c:98-126`,
    about 7 ms at 115200). Keep kprintf and IOLog out of the RX path, and
    run the stress test without `-enable_kprintf_spam`.
- An H4 deframer runs in the worker thread. It accepts only event (0x04)
  and ACL (0x02): the controller never sends commands, and SCO is not used.
  ACL length is capped at 1021 bytes, so a corrupt length cannot swallow the
  stream. On a framing error it resyncs at the next plausible type byte and
  counts the drop.
- TX is polled (wait for FR.TXFF clear). Commands are small; ACL TX later can
  move to TXIM.
- **Baud switch:** wait for FR.BUSY to clear, write IBRD and FBRD, then LCRH,
  so the divisor latches (`amba-pl011.c:2252-2262`). Flush the RX ring and
  reset the deframer. Linux adds no delay after the switch
  (`hci_bcm.c:624-626`); a retry of the first command after it is enough.
- **Speed:** 921600 by default; boot-arg `bt-baud=` overrides it. 2000000 is
  only worth trying after the overrun counters stay at 0 at 921600.

### B3. Bring-up sequence (kernel, on `up` from userland)

Order: Linux's `no_early_set_baudrate` variant. The whole patch goes over
at 115200 (about 2.7 s), with one speed switch at the end. This deviates
from what Linux does on the 3B: `bcm43438_device_data` has no
`no_early_set_baudrate` (`hci_bcm.c:1576-1578`), so Linux switches before
the download (`hci_serdev.c:186-211`) and again after it
(`hci_bcm.c:603-626`). The deviation is deliberate: one switch, made after
the controller is known to be running the patch, is one fewer place to lose
the link. The early switch is a later optimisation.

1. Power cycle (B1); set 115200.
2. `HCI_Reset` (0x0c03), wait for Command Complete; then 100 ms
   (`btbcm_reset`, `btbcm.c:294`).
   - 2b. `Read_Local_Version` (0x1001). Refuse anything but LMP subversion
     0x2209 (BCM43430A1, `btbcm.c:512`), as `btbcm_initialize` reads before
     patching (`btbcm.c:589-634`).
3. `Download_Minidriver` (0xfc2e), then 50 ms.
4. Walk the `.hcd`: records of `{u16 opcode, u8 plen, plen bytes}`. Send each
   record as an HCI command and wait for its Command Complete. Bounds-check
   every record (`btbcm_patchram`: "Patch is corrupted").
   The pinned file is 121 records: 120 × `0xfc4c` (Write_RAM) and a final
   `0xfc4e` (Launch_RAM), with a maximum plen of 255. Check that the last
   record is `0xfc4e`.
5. Wait 250 ms. The controller reboots into the patched firmware, still at
   115200.
6. `HCI_Reset` again, then 100 ms (`btbcm_finalize` → `btbcm_initialize` →
   `btbcm_reset`).
7. `Update_UART_Baud_Rate` (0xfc18, `{u16 0, u32 baud}`), wait for Command
   Complete, then switch the PL011. Skip `0xfc45` (only for > 3 Mbaud,
   `bcm_set_baudrate`).
8. `Read_Local_Version` and `Read_BD_ADDR` as a smoke check.
   - The chip defaults are `43:43:A1:12:1F:AC` and `AA:AA:AA:AA:AA:AA`
     (`btbcm.c:28,36`). Linux only flags these as invalid
     (`btbcm_check_bdaddr`); Raspberry Pi OS gets a real address from its
     firmware through a `local-bd-address` DT override
     (`bcm271x-rpi-bt.dtsi:8-9`).
   - Project choice, no upstream equivalent: if the address is a default,
     derive one from the board MAC (`BCM2837Mailbox::getBoardMACAddress`,
     last byte + 1) and set it with `0xfc01` (`btbcm.c:159`).
9. `Set_Event_Mask` (0x0c01) with LE Meta (bit 61) and Extended Inquiry
   Result (bit 46) enabled, plus `LE_Set_Event_Mask` (0x2001). The default
   mask leaves both out, so `bt scan` would see neither LE reports nor EIR
   names. (Bluetooth Core spec; not on disk.)

Userland pushes the firmware through the user client, as for WiFi, so no
filesystem access is needed in the kernel. The kernel parses and sends it.

### B4. User client `BCM43430BluetoothUserClient`

Shared ABI header `BCM43430BluetoothShared.h`. All methods require root.

- `uploadBegin` / `uploadChunk` / `up`: firmware load plus steps 1-8.
- `sendCommand(opcode, params ≤ 255)`: one command in flight at a time.
  Returns the matching Command Complete or Command Status event, with a 2 s
  timeout.
  - Refuses opcodes the driver owns: `0xfc18` and `0xfc45` (a speed change
    would leave the host at the old rate and the link dead until power
    cycle), `0xfc2e`/`0xfc4c`/`0xfc4e` (patch), and `HCI_Reset` (use `up`).
  - Copes with Num_HCI_Command_Packets = 0 and the NOP (opcode 0) Command
    Complete.
- `readEvent(timeout)`: next asynchronous event or ACL packet from the queue
  (inquiry results, LE advertising reports). The queue is bounded, and drops
  on overflow are counted, not hidden.
- `status`: state, BD_ADDR, firmware name (from `Read_Local_Name` after the
  patch), baud, RX overrun, framing and drop counters. Open to non-root,
  like WiFi's status, because the launchd job checks it. It sends no HCI
  traffic.
- The client is exclusive (one event queue). Structs are fixed-size, with
  exact size checks (WiFi's `NEED_IN`/`NEED_OUT`).

### B5. `bt` userland tool

`tools/userland_staging/bt.c` + `build_bt.sh`, dynamic, same shape as
`wifi.c`.

- `up [file]` (default `/usr/share/firmware/bluetooth/BCM43430A1.hcd`)
- `info`
- `scan [secs]`: classic `HCI_Inquiry` (0x0401, LAP 0x9e8b33) with
  `Write_Inquiry_Mode` = extended, plus LE: `LE_Set_Scan_Parameters`
  (0x200b) and `LE_Set_Scan_Enable` (0x200c). Print BD_ADDR, RSSI, and the
  name from EIR/AD.
- `hci <ogf.ocf|opcode> [hex bytes...]`

`fetch_bt_firmware.sh` pins the commit and SHA-256 above into
`libc_build/bt_firmware/` (gitignored). `inject_into_sd_image.sh` installs the
tool and firmware. An `org.puredarwin.bluetooth` job runs `bt up` when booted
with `bt=1`, following the WiFi job.

## Testing

- **QEMU:** the PL011 is hard-wired to `serial_hd(0)`
  (`bcm2835_peripherals.c:194`) and the console mini-UART to `serial_hd(1)`.
  `runvm.sh:191` passes `-serial null -serial stdio`. Three changes are
  needed first:
  1. A runvm knob (`BT_SERIAL=unix:...`) replaces the first `-serial null`.
  2. Our qemu-zhuowei fork answers tag `0x00038041` with status 0. Today it
     falls to `default:` and leaves 128 in the word
     (`bcm2835_property.c:280-283`), which the driver correctly reads as
     failure.
  3. The fork reports clock id 2 as 48 MHz, not 3 MHz
     (`bcm2835_property.c:118-119`). At 3 MHz, 921600 gives IBRD = 0.

  Both QEMU changes are committed and pushed in the fork, per the fork rule.
  Then a checked-in Python H4 responder (`tools/bt_h4_responder.py`) on the
  socket answers Reset, Read_Local_Version (0x2209), Minidriver, each patch
  record, the baud change, Read_BD_ADDR and the event masks.

  This tests framing, parsing and the user client, not timing or overruns:
  QEMU's PL011 ignores the baud rate and applies backpressure
  (`pl011_can_receive`), so it never produces OE.
- **Real Pi:** with `bt=1`, check that `bt up` loads the patch, `bt info`
  shows a sane LMP version and BD_ADDR, and `bt scan` sees a nearby phone (LE
  advertising and classic in discoverable mode). Run a stress scan for
  several minutes at the chosen baud with the overrun and framing counters
  at 0. Check that WiFi keeps working (ping plus bulk scp) while BT scans,
  since the two radios share 2.4 GHz.

## Risks

1. **No flow control:** the RX FIFO may overrun under interrupt latency
   (kprintf, masked-interrupt windows). Mitigations: 921600 default, measure
   the counters, fall back lower. Scan and HCI traffic are low bandwidth.
   GPIO30/31 are named "CTS0/RTS0 Used by BT module" in the 3B line names
   (`bcm2837-rpi-3-b.dts:86-88`), so muxing them ALT3 is an experiment
   worth trying if overruns appear.
2. **GPU IRQ 57 routing:** only 62 is proven in bank 2; sdhost's 56 is
   declared but never registered. The vector arithmetic is right, and the
   basic-pending alias is masked `&0xFF` (`BCM2837InterruptController.cpp:262`),
   so there is no double dispatch. Verify on QEMU first.
3. **Firmware routing PL011 elsewhere:** confirm on hardware that nothing
   (u-boot, firmware `enable_uart`) leaves GPIO14/15 on PL011 or 32/33 on
   ALT5. We set the pins ourselves anyway.
4. **The patch never answers** (wrong `.hcd` for this chip revision): `Read_Local_Version`
   before the download identifies the chip (`btbcm.c` table:
   BCM43430A1 subver 0x2209). Refuse a mismatch with a clear error.

## Steps

- **B1** expander GPIO helper + pinmux.
- **B2** PL011 driver + DT node + personality, behind `bt=1`; tested on QEMU
  with the H4 responder.
- **B3** bring-up sequence.
- **B4** user client.
- **B5** `bt` tool, fetch script, image injection, launchd job.
- **B6** real-hardware verification and the coexistence check.

## Changes after review (2026-10-12)

The adversarial review's findings are folded into the sections above.
- **QEMU test:** needs two qemu-zhuowei fork fixes (the BT_ON tag and the
  UART clock) and a runvm serial knob. The PL011 is `serial_hd(0)`, not a
  second `-serial`.
- **Bring-up:** a 120 ms power-on wait; `Read_Local_Version` before
  patching; 100 ms after the second reset. The patch-then-switch order is
  now stated as a deliberate deviation from Linux's 3B order.
- **Speed:** 921600, not 2M (downstream 3B override).
- **RX design:** drain-clears RX/RT; loop on MIS; SPSC ring; timed worker
  sleep; no kprintf in the path.
- **Baud switch:** IBRD/FBRD then LCRH; BUSY wait; flush the ring.
- **BD_ADDR:** the derivation is a project choice, not a citation.
- **User client:** refuses the driver-owned opcodes; exclusive; status
  readable by non-root; handles NOP/zero-credit Command Complete.
- **Event masks:** LE Meta and EIR enabled.
- **Naming:** the DT node is `bt-uart` (avoids `pe_serial.c`'s `uart0`
  fallback); a single driver name; `iokit/conf/files` entries.
- **GPIO lock:** added to `BCM2837GPIO` in the WiFi review fixes.
