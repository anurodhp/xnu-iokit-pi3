# USB host controller support: what Ethernet is actually blocked on

**ARCHIVED 2026-08-27 — superseded, kept for history only.** All of this
plan's tasks are done and verified on real hardware, not just QEMU:
DWC2 bring-up, enumeration, and the hub protocol subset (Phase 6a, DONE
2026-08-21, QEMU-verified per the STATUS section below); real-hardware
LAN9514 enumeration and `SMSC9514USBTransport` (DONE 2026-08-22, see
project history); interrupt-driven completion is the one item from this
plan still genuinely open, tracked live as `docs/backlog.md` item 0. For
current status see `docs/roadmap.md`'s Phase 6a/6b sections; this file's
own STATUS/task list below is a historical snapshot from when USB was
still blocking Ethernet.

## STATUS 2026-08-21: tasks 1-4 DONE and QEMU-verified; 5-6 written, not verified

Implemented and confirmed on a real QEMU boot (`tools/boot_test.py`, full
boot to `login:`, no panic):

- `iokit/BCM2837DWC2/BCM2837DWC2.cpp` - DWC2 core/host bring-up and the
  control/bulk/interrupt transfer engine, ported from
  `third_party/u-boot/drivers/usb/host/dwc2.c` (tasks 2-3).
- `iokit/BCM2837DWC2/BCM2837USBEnum.cpp` - device enumeration and the hub
  protocol subset (task 4).
- `devicetrees/rpi3.dts` `usb` node + personality (tasks 1, 6).

Evidence from the boot log, not inference:

```
BCM2837DWC2: GSNPSID=0x4f54294a GHWCFG2=0x250dc016 GHWCFG3=0x10000044
BCM2837DWC2: DMA buffer virt=0xfffffff8858cd000 phys=0xa5d4000 bus=0xca5d4000
BCM2837DWC2: core init done, 8 host channels
BCM2837DWC2: root port enabled, full-speed device attached (HPRT0=0x2100d)
BCM2837DWC2: device 1: 0409:55aa class 9, ep0 mps 8, full-speed (hub 0 port 0)
BCM2837DWC2: hub at address 1 has 8 ports (power-good 20ms)
BCM2837DWC2: device 2: 0525:a4a2 class 2, ep0 mps 64, full-speed (hub 1 port 1)
BCM2837DWC2: enumeration complete, 2 device(s)
```

That is a real GSNPSID (`0x4f54...` = "OT"), a real bus address (`0xca5d4000`
= ARM physical `0xa5d4000` through the `0xc0000000` VC alias), a real device
descriptor read from the root-port device, and - the part this plan's
"hub-in-the-path" note existed to force - **a second device enumerated
through a downstream hub port**, which is the exact topology the LAN9514
presents. This meets and exceeds the exit criteria below.

### What is NOT verified

- **Task 5, `SMSC9514USBTransport`, is written but unverified.** QEMU models
  no LAN9514, so `runvm.sh` substitutes a generic `usb-net`. Everything
  under the SMSC register protocol is exercised for real; the SMSC protocol
  itself is not, and cannot be here. It needs real hardware - now possible,
  a serial cable is available as of 2026-08-21.
- **Interrupt-driven completion.** Task 2 called for mirroring
  `BCM2837SDHCI`'s interrupt-driven design; this first cut POLLS, with
  global interrupts deliberately masked, exactly as the u-boot reference
  does. Doing register bring-up and interrupt plumbing in one change would
  have meant debugging two unproven things at once, which is precisely how
  `BCM2837SDHCI`'s interrupt work burned three passes. Contained follow-up.
- **Split transactions.** Implemented (required whenever a full/low-speed
  device sits behind a high-speed hub) but never executed: QEMU's `usb-hub`
  is full-speed, so no split is needed, and the LAN9514's Ethernet function
  is high-speed, so real hardware may not exercise it either.
- Cascaded hubs, hot-plug and re-enumeration are out of scope by design.

### Prerequisite discovered mid-implementation, not in the original plan

The "Exit criteria" section below guessed that `third_party/qemu-zhuowei`
"already emulates a DWC2 device given `-M raspi3`'s usual peripheral set".
**That was wrong.** The fork is based on QEMU 2.12.91; upstream QEMU only
gained a DWC2 model in 5.2, and this tree's `bcm2835_peripherals.c` maps
nothing at `USB_OFFSET`, so `0x3f980000` read back as unassigned memory.
With real-hardware boot then still blocked on serial, there was no way to
test a USB stack at all. Backporting the model became step 0 - see the
`hw/usb: backport DWC2 host controller from QEMU 5.2` commit in the
qemu-zhuowei fork.



Written 2026-08-11. `docs/sd-and-ethernet-plan.md` already identified that
Ethernet (`iokit/SMSC9514Ethernet/`) cannot send or receive a single packet
without a working USB host controller, and flagged that as "a materially
larger undertaking than everything else in this repository combined" - but
never scoped that undertaking as its own plan. This document is that plan.
Nothing below changes `SMSC9514Ethernet`'s status: its chip-level protocol
logic is done and waiting behind the `SMSCUSBTransport` abstract interface
(`iokit/SMSC9514Ethernet/SMSC9514Ethernet.h`); this document is entirely
about what has to exist to give that interface a real implementation.

## Why USB has to come first (restated concretely)

```
SMSC LAN9514 (USB hub + Ethernet, one chip, wired to the Pi3's single
              USB port - see "hub-in-the-path" note below)
  needs USB device enumeration (control transfers, descriptors, addressing)
    needs USB hub protocol (the LAN9514 enumerates AS a hub; the Ethernet
                             function sits behind one of its own ports)
      needs a working USB host controller driver
        = DWC2 (Synopsys DesignWare USB2 OTG), the BCM283x's actual silicon
```

Every layer above DWC2 is unreachable until DWC2 can enumerate at least one
device. There is no way to sequence Ethernet work ahead of this.

**Hub-in-the-path note, not previously called out**: the LAN9514 is not a
plain USB Ethernet dongle - it's a combo hub+Ethernet chip, and on the Pi 3
the Ethernet function is wired behind the chip's own internal hub port, not
exposed directly on the root port. This means "USB device enumeration"
alone is not sufficient scope for reaching Ethernet - **hub descriptor
parsing and downstream port power/reset/attach handling are required too**,
not just root-port control/bulk transfers. This is a real scope addition
versus what a plain-device USB stack would need; call it out explicitly so
it isn't rediscovered mid-implementation.

## What already exists (found this session)

- **`third_party/u-boot/drivers/usb/host/dwc2.c`** (1375 lines) +
  `dwc2.h` (790 lines) - a real, working DWC2 host controller driver,
  already vendored in this repo's own u-boot checkout (u-boot uses it to
  drive the Pi's USB port before handing off to XNU). This is a
  significantly better reference than "port from Linux" (the pattern used
  for `BCM2837SDHCI`/`SMSC9514Ethernet`): it's already in-tree, already
  proven against this exact SoC (BCM2837) by u-boot's own boot chain
  (`u-boot+xnu`, see project memory), and covers register init, channel
  setup, and control/bulk/interrupt transfer submission end to end. Treat
  this as the primary port target, the same way Linux's `sdhci-iproc.c`
  was the primary reference for the SD driver - not guessed, a real
  working implementation to read register-for-register.
- **`third_party/IOUSBFamily`** (`apple-oss-distributions/IOUSBFamily`,
  gitignored) - real Apple IOKit USB family source: `IOUSBController`/
  `IOUSBControllerV2` (abstract base classes host controller drivers
  subclass), plus concrete reference drivers `AppleUSBEHCI` (3294 lines),
  `AppleUSBOHCI` (6405 lines), `AppleUSBUHCI`, `AppleUSBXHCI`. No DWC2
  variant - confirmed absent, matching the existing plan's finding. These
  numbers matter for scoping: a full `IOUSBController` subclass done to the
  same standard as Apple's own host controller drivers is a multi-thousand
  line undertaking, independent of DWC2 register logic itself (that part's
  scope is roughly bounded by the 1375-line u-boot reference above).
  `AppleUSBHub` exists in this same tree but as a project file only (`.plist`,
  no vendored source in this checkout) - the real hub *driver* (as opposed
  to hub *protocol*, which is just control transfers any host stack can
  issue directly) isn't available as a drop-in the way EHCI/OHCI are.
- **No USB node in `devicetrees/rpi3.dts` at all** (checked directly -
  no `usb@` node, no `dwc2`/`dr_mode` string anywhere in the file). Every
  other driver in this project (`BCM2837SDHCI`, the interrupt controllers)
  matches against a real DT node; USB has no DT node to match against yet,
  so this is a prerequisite step, not an afterthought.
- **`SMSCUSBTransport`** (`iokit/SMSC9514Ethernet/SMSC9514Ethernet.h`) - the
  4-method pure-virtual interface (`readRegister`/`writeRegister` via
  vendor control transfers, `bulkRead`/`bulkWrite` for frame data) that
  whatever this plan produces must ultimately hand a concrete implementation
  of to `SMSC9514Ethernet::init()`. Already scoped to exactly what
  `smsc95xx.c`-derived logic needs, nothing more - a useful upper bound on
  what the USB stack needs to expose, separate from what it needs
  internally to talk to DWC2.

## Architecture decision: full `IOUSBController` family vs. minimal purpose-built stack

Two real options, not previously weighed against each other:

**Option A - subclass `IOUSBController`/`IOUSBControllerV2`, fit into the
real Apple IOUSBFamily hierarchy.** Gets real compatibility with
`AppleUSBHub`-style hub drivers and any future IOKit USB client (mass
storage, HID) essentially for free once written, because it speaks the same
internal protocol those classes expect. Cost: matches the ~3300-6400 line
scope of Apple's own EHCI/OHCI drivers above, largely to satisfy
`IOUSBController`'s abstract surface (isochronous scheduling, full
power-management state machine, etc.) that this project will never
exercise on a single wired Ethernet chip - most of that surface is dead
weight for this project's actual goal.

**Option B - minimal purpose-built USB stack**, same pattern already
chosen for Ethernet itself (`SMSCUSBTransport`'s narrow interface instead
of wiring into a hypothetical generic USB device-driver family). Implement
only: DWC2 register init/channel management (ported from the u-boot
reference), root-port device enumeration (control transfers, GET_DESCRIPTOR,
SET_ADDRESS, SET_CONFIGURATION), and the specific hub protocol subset
needed to reach a device behind one hub port (GET_HUB_DESCRIPTOR,
SET_PORT_FEATURE for power/reset, GET_PORT_STATUS to detect attach) - then
hand bulk/control transfer primitives directly to a `SMSCUSBTransport`
implementation. No generic `IOUSBController` subclassing, no isochronous
support (unneeded - Ethernet bulk/interrupt only), no hot-plug hub-cascade
generality beyond the one hub this board actually has wired in.

**Recommendation: Option B**, for the same reason `SMSCUSBTransport` itself
was scoped narrowly rather than against a hypothetical generic USB device
family - this project's actual goal is one Ethernet chip working, not a
general-purpose USB subsystem, and Option A's extra surface (isochronous,
full PM state machine, arbitrary hub cascades) has no consumer in this
project's roadmap (`docs/roadmap.md`'s Phase 2 keyboard-via-USB stretch
goal only needs boot-protocol HID reports, not a general driver family
either - see that file's existing note). If a second USB device class ever
becomes a real requirement (e.g. real HID from Phase 2's stretch goal),
revisit Option A then with a second real consumer to justify the cost -
don't build the general form speculatively now.

## Concrete tasks, in the order that actually unblocks progress

1. **Devicetree**: add a `usb@7e980000` node (BCM2835/2837 DWC2 MMIO base;
   confirm exact offset against `third_party/u-boot`'s own `.dts`/config
   before trusting from memory) to `devicetrees/rpi3.dts`, with
   `compatible = "brcm,bcm2835-usb"` (the string Linux's `dwc2` driver and
   u-boot both match against - verify against u-boot's own DT usage rather
   than assuming). Follow the same pattern already used for
   `brcm,bcm2835-sdhci`/`brcm,bcm2835-mbox` in that file.
2. **DWC2 register/transfer layer**: port from
   `third_party/u-boot/drivers/usb/host/dwc2.c`/`.h` - core init sequence,
   channel allocation, control/bulk/interrupt transfer submission,
   IRQ-driven completion (mirror the interrupt-driven pattern already used
   in `BCM2837SDHCI` rather than u-boot's own polling loops, consistent
   with how this project's SD driver diverged from its polling reference).
3. **Root-port device enumeration**: GET_DESCRIPTOR (device/config),
   SET_ADDRESS, SET_CONFIGURATION - standard USB control-transfer sequence,
   well-defined, not chip-specific.
4. **Hub protocol subset** (see "hub-in-the-path" note above - required,
   not optional, for this specific chip): hub descriptor fetch, per-port
   power-on, per-port reset, port-status polling to detect the Ethernet
   function's attach - enough to reach one device behind one hub port, not
   a general hub-cascade driver.
5. **`SMSCUSBTransport` concrete implementation**: wire steps 2-4's
   primitives into the 4-method interface `SMSC9514Ethernet::init()`
   already expects (`readRegister`/`writeRegister` as vendor control
   transfers, `bulkRead`/`bulkWrite` for frame data) - this is the point
   where Ethernet stops being blocked.
6. **IOKit personality + provider wiring**: register a personality
   matching the new `usb@` DT node (same `BCM2837Personalities.cpp` pattern
   as every other driver in `iokit/BCM2837ArmIO/`), publish the DWC2
   controller as the provider `SMSC9514Ethernet` (via its transport) probes
   against.

## Compile/test verification caveat

Same class of environment gap already documented for SD/Ethernet in
`docs/sd-and-ethernet-plan.md`: if this driver ends up including any of
`IOUSBFamily`'s real headers (Option A, or even Option B if it borrows
shared USB descriptor-struct definitions from that family rather than
hand-rolling them), expect the same `IOTypes.h` -> `mach_interface.h` SDK
header-skew wall already hit by `IOStorageFamily`. Same resolution path:
either hand-rolled minimal struct definitions (own descriptor structs,
verified against the USB spec directly rather than pulled from the family),
or the same unresolved `make installhdrs`/matching-SDK fix already pending
for the SD driver.

## Exit criteria

DWC2 controller initializes without hanging against real QEMU
(`third_party/qemu-zhuowei`, which already emulates a DWC2 device given
`-M raspi3`'s usual peripheral set - confirm this before assuming
real-hardware-only testing is required); a `GET_DESCRIPTOR` control
transfer to the root-port device (the LAN9514 acting as a hub) returns a
real, parseable device descriptor; the hub's Ethernet-function port can be
powered/reset and its own device descriptor read back. Reaching a
parseable descriptor for the *Ethernet function itself* (behind the hub
port) is the actual point at which `docs/sd-and-ethernet-plan.md`'s
Ethernet work stops being blocked and `SMSCUSBTransport` can be
implemented for real.

## Sequencing

This plan is a hard prerequisite for `docs/sd-and-ethernet-plan.md`'s
Ethernet section - that section already says as much in prose ("the actual
next milestone for Ethernet is a USB host controller driver"); this file
makes it a gate: **do not resume `SMSC9514Ethernet` transport work until
this plan's exit criteria are met.** SD-card work (`docs/roadmap.md` Phase
3/3.5) has no dependency on this file and is unaffected.
