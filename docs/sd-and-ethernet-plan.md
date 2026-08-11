# SD card + Ethernet: what's needed after XNU boots

Written while the main investigation thread was separately debugging the
QEMU direct-boot panic. Assumes that gets resolved and XNU reaches a
running, interactive state; this plan doesn't depend on when that happens.

## SD card (`iokit/BCM2837SDHCI/`)

**Written this session** — a real `IOBlockStorageDevice` subclass for the
Arasan SDHCI controller (`sdhci` DT node), covering the SD initialization
sequence (CMD0/8/ACMD41/CMD2/3/9/7/16) and PIO block read/write
(CMD17/18/24/25 + STOP_TRANSMISSION). Register offsets and BCM2835-specific
quirks (no card-detect line, untrustworthy CAPABILITIES register, no
high-speed/UHS support) are sourced from Linux's `drivers/mmc/host/sdhci.h`
and `sdhci-iproc.c`, not guessed. CSD (capacity) parsing bit-math was
verified against Linux's actual `mmc_decode_csd()`/`unstuff_bits()`
implementation rather than derived from memory of the SD spec.

Remaining work, roughly in the order it actually blocks progress:

1. **GPIO pinmux** — nothing currently remuxes GPIO48-53 to ALT3 to route
   the Arasan controller to the physical SD slot (it defaults to the
   onboard SDIO WiFi instead; see `devicetrees/rpi3.dts` and project
   memory for the full explanation). Without this, every command in
   `BCM2837SDHCI` will just time out. This is the actual first blocker,
   not a large amount of work - direct GPIO `FSEL` register pokes using
   the macros already in `third_party/xnu-7195/pexpert/pexpert/arm64/BCM2837.h`
   (`BCM2837_FSEL_REG`, `BCM2837_FSEL_ALT3`), done either in this driver's
   `start()` or in earlier platform init.
2. **Interrupt wiring** — done. `start()` calls `registerInterrupt()`/
   `enableInterrupt()` against the provider; `interruptOccurred()` is a
   primary handler mirroring Linux's `sdhci_irq()`, and command/buffer-ready
   waits block on it via `waitForIntStatus32()` instead of busy-polling
   `INT_STATUS`. Data movement itself is still PIO (no DMA). Like the rest
   of this driver, never run against real hardware yet - GPIO pinmux (above)
   has to land first before any interrupt actually has a chance to fire.
2. **Filesystem**: XNU has no ext4 (or ext2/3) support out of the box.
   Whatever's actually going to be readable from the SD card needs to
   either be a filesystem XNU already understands (HFS+/APFS, or FAT if a
   FAT kext is available in this fork), or a filesystem kext needs to be
   added separately. Not resolved by this plan - flagging it because it's
   easy to lose track of: a working block driver alone doesn't get you a
   mountable volume if nothing in the kernel can parse what's on it.
3. **`IOStorageFamily` isn't in the base `xnu` source tree.** This was the
   first real surprise this session: `IOBlockStorageDevice`/
   `IOBlockStorageDriver`/`IOMedia` (the actual kernel-side C++ classes)
   don't exist anywhere in `third_party/xnu-7195` - only a DriverKit
   (userspace) compat stub does. The real headers had to be pulled
   separately from `apple-oss-distributions/IOStorageFamily` (cloned to
   `third_party/IOStorageFamily`, not committed - gitignored under the
   blanket `/third_party/` rule same as everything else there). Whatever
   actually builds the final kernel needs `IOBlockStorageDriver` +
   `IOPartitionScheme`/`IOFDiskPartitionScheme` compiled in too (from that
   same family) to get from "one `IOBlockStorageDevice` nub" to "an
   actual mountable `IOMedia`/partition" - this driver alone publishes
   the nub, it doesn't do partition scheme matching itself (that's
   `IOBlockStorageDriver`'s job per the family's own architecture).
4. **Compile verification is blocked on a pre-existing environment gap**,
   not a defect in the driver's logic. `IOBlockStorageDevice.h`'s
   `#include <IOKit/IOTypes.h>` chain reaches `mach_interface.h`, which
   pulls in the live installed SDK's `<mach/mach_port.h>` →
   `<mach/mach_host.h>` → `<mach/task.h>`... - an open-ended tail of
   newer Mach IPC types this project's pinned 2019-era xnu checkout
   doesn't define (same class of problem as the MIG-generated headers
   already documented in project memory, just a longer tail). Confirmed
   this isn't specific to the new driver by running the *already-verified*
   `BCM2837InterruptController.cpp` through the identical flags - it
   doesn't hit this wall only because its narrower include chain
   (`IOInterruptController.h` → `IOLocks.h`) happens not to reach
   `mach_interface.h`'s heavier includes, not because the underlying SDK
   skew doesn't apply to it too. Verified `BCM2837SDHCI`'s own logic
   (register math, control flow, the 13 override signatures) instead by
   compiling against a hand-written stand-in for `IOBlockStorageDevice`
   with the identical class shape (signatures copied verbatim from the
   real header) - genuinely type-checks the driver's own code, just not
   through the real header's full transitive closure. Resolving this for
   real needs the same fix as the MIG-header gap: either a matching-vintage
   SDK, or running xnu's actual `make installhdrs` (which needs the
   `ctfconvert`/SDK-header-sync step already documented as paused pending
   user go-ahead in project memory).

## Ethernet (`iokit/SMSC9514Ethernet/`)

**GATED on `docs/usb-support-plan.md`.** Do not resume `SMSC9514Ethernet`
transport work until that plan's exit criteria are met (a real USB device
descriptor read back from the LAN9514's Ethernet function, behind its own
hub port). Everything below is unchanged from when it was written, but
should be read as background/scope, not as a currently-actionable next
step - the actionable next step for Ethernet is that other document.

**Written this session, but genuinely can't be tested or even fully
compile-verified yet** - the real dependency chain is longer than SD:

```
SMSC LAN9514 (USB Ethernet+hub chip)
  needs USB device/endpoint enumeration
    needs a working USB host controller driver
      = DWC2 (Synopsys DesignWare USB2 OTG), BCM283x's actual silicon
```

**No DWC2 host controller driver exists anywhere accessible to this
project.** Checked `apple-oss-distributions/IOUSBFamily` (cloned to
`third_party/IOUSBFamily`) - it ships EHCI/OHCI/UHCI/XHCI host controllers
only. Apple's real DWC2-equivalent driver for their own embedded/mobile
SoCs was never open-sourced. Writing one from scratch means implementing,
against the raw Synopsys IP register set, everything a USB host controller
does: device/config descriptor parsing, hub and endpoint enumeration,
transfer scheduling for control/bulk/interrupt/isochronous. That's a
materially larger undertaking than everything else in this repository
combined - realistically its own multi-week project, not something to
bolt on here.

Given that, **this driver is deliberately split at the USB boundary**
rather than either skipped or half-faked:

- `SMSC9514Ethernet` (the actual class) contains all of the chip-specific
  logic ported from Linux's `drivers/net/usb/smsc95xx.c`/`.h` - reset
  sequence, MDIO/PHY register access, MAC address programming, RX/TX frame
  framing (the `TX_CMD_A`/`TX_CMD_B`/`RX_STS` word conventions those
  chips use). This talks only to an abstract `SMSCUSBTransport` interface,
  never touching USB/DWC2 specifics directly.
- `SMSCUSBTransport` is a 4-method pure-virtual interface (register
  read/write via USB vendor control transfers, bulk IN/OUT for packet
  data) - exactly the surface `smsc95xx.c` actually needs from its USB
  layer, nothing more. Whatever eventually implements DWC2 + enumeration
  provides a concrete subclass and hands it to `SMSC9514Ethernet::init()`.

This means the Ethernet *protocol* logic is real, referenced against
actual Linux source (register offsets/bits verified via
`gh api .../smsc95xx.h`, not recalled from memory), and could be unit
tested today against a stub/loopback `SMSCUSBTransport` - but it cannot
send or receive one real packet until DWC2 exists. Said plainly rather
than glossed over: **writing more Ethernet-layer code right now would be
premature** - the actual next milestone for Ethernet is a USB host
controller driver, which is its own project, not an extension of this one.
Same environment-gap caveat applies to compile verification as the SD
driver (see above) - even less far to test given it can't run regardless.

## Sequencing recommendation

1. GPIO pinmux for the SD slot (small, concrete, already understood -
   the actual unblocking step for SD).
2. Get `BCM2837SDHCI` reading real blocks once pinmux is in.
3. Add `IOBlockStorageDriver`/partition scheme (from the newly-found
   `IOStorageFamily`) so a real `IOMedia`/mountable volume appears, and
   settle what filesystem is actually on the card.
4. Ethernet is a separate, larger, later project starting with DWC2 - not
   next in line after SD. `SMSC9514Ethernet`'s protocol logic is done and
   waiting, but there's a full USB host controller driver between here and
   a working network interface. See `docs/usb-support-plan.md` for that
   driver's own scoped plan - Ethernet is formally gated on it.
