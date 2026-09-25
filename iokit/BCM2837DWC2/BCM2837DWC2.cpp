/*
 * BCM2837 DWC2 USB 2.0 OTG host controller. See BCM2837DWC2.h for scope,
 * references, and known limitations. (Completion has been interrupt-driven
 * since DAR-136, and parked async IN transfers exist since DAR-370.)
 */

#include "BCM2837DWC2.h"
#include "SMSC9514USBTransport.h"
#include "BCM2837Mailbox.h"
#include "BCM2837USBHostController.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/network/IONetworkStack.h>
#include <pexpert/pexpert.h>

// iokit/bsddev/IOKitBSDInit.cpp (real Apple source, kernel-private). Used
// to be `static` and only reachable from within that file for the
// netboot-root case - see the long comment at the call site below for why
// it had to be exposed here too. extern "C" because IOKitBSDInit.cpp
// defines it inside its own top-level `extern "C" { ... }` block.
extern "C" bool IORegisterNetworkInterface(IOService *netif);

#define super IOService
OSDefineMetaClassAndStructors(BCM2837DWC2, IOService);

// The transfer staging buffer. DWC2's HCTSIZ.XFERSIZE is 19 bits and
// HCTSIZ.PKTCNT 10 bits, so the silicon can describe far more than this in
// one go; 64KB simply covers every transfer this stack issues (the largest
// being a full config descriptor fetch or an Ethernet frame) with room to
// spare, and keeps one physically-contiguous allocation small enough to be
// reliable.
#define kDWC2DataBufferSize     (64 * 1024)

// The synchronous path's channel. Channels 1..fAsyncChannelCount carry
// parked async IN transfers (DAR-370) - see BCM2837DWC2.h.
#define kDWC2Channel            0

// Per-chunk ceiling from the silicon's own counters, mirroring u-boot's
// CONFIG_DWC2_MAX_PACKET_COUNT / CONFIG_DWC2_MAX_TRANSFER_SIZE.
#define kDWC2MaxPacketCount     511
#define kDWC2MaxTransferSize    65535

/*
 * ARM physical -> VideoCore bus address. The DWC2 core is an AHB bus
 * master and HCDMA takes a *bus* address, not an ARM physical one. On
 * BCM2836/2837 the uncached-alias bus window is 0xc0000000; verified
 * against third_party/u-boot/arch/arm/mach-bcm283x/phys2bus.c (which
 * returns 0x40000000 instead only for the original BCM2835), not recalled.
 * QEMU's model agrees: bcm2835_peripherals wires the controller's DMA
 * through gpu_bus_mr, whose RAM aliases sit at 0, 1GB, 2GB and 3GB.
 */
#define kBCM2837BusAddressBase  0xc0000000ULL

// FIFO depths in 32-bit words, from u-boot's dwc2.h defaults for this core.
#define kDWC2RxFIFOSize         (516 + 16)
#define kDWC2NPTxFIFOSize       0x100
#define kDWC2PTxFIFOSize        0x200

#pragma mark - Register access

uint32_t
BCM2837DWC2::regRead(uint32_t offset) const
{
	return *(volatile uint32_t *)(fRegBase + offset);
}

void
BCM2837DWC2::regWrite(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(fRegBase + offset) = value;
}

void
BCM2837DWC2::regModify(uint32_t offset, uint32_t clearBits, uint32_t setBits)
{
	uint32_t v = regRead(offset);
	v &= ~clearBits;
	v |= setBits;
	regWrite(offset, v);
}

uint32_t
BCM2837DWC2::hcOffset(uint32_t chan, uint32_t reg) const
{
	return kDWC2_HCCHAR_BASE + chan * kDWC2_HC_STRIDE + reg;
}

/*
 * Bounded poll. Deliberately mixes IODelay and IOSleep: the first
 * millisecond is spun on, because the overwhelming majority of these waits
 * (FIFO flush, core reset, channel halt on a local transfer) complete in
 * microseconds and a full scheduler round-trip per poll would dominate; past
 * that it sleeps, so a genuinely slow or absent device cannot burn a core
 * for seconds. Callers must therefore be in a context where sleeping is
 * legal - which is true of all of them, since nothing here runs at
 * interrupt level (see BCM2837DWC2.h's note on polled completion).
 */
bool
BCM2837DWC2::waitForBits(uint32_t offset, uint32_t mask, bool set, uint32_t timeoutMS)
{
	const uint32_t spinIterations = 1000; // 1000 x 1us

	for (uint32_t i = 0; i < spinIterations; i++) {
		uint32_t v = regRead(offset) & mask;
		if (set ? (v == mask) : (v == 0)) {
			return true;
		}
		IODelay(1);
	}

	for (uint32_t ms = 0; ms < timeoutMS; ms++) {
		uint32_t v = regRead(offset) & mask;
		if (set ? (v == mask) : (v == 0)) {
			return true;
		}
		IOSleep(1);
	}

	return false;
}

/*
 * waitForBits() waits for EVERY bit in the mask. This waits for ANY of them,
 * which is what a status register carrying several mutually exclusive
 * outcomes needs - HCINT in particular, where the caller has to know which of
 * the possible answers arrived, not wait for all of them at once.
 *
 * Same spin-then-sleep shape as waitForBits: a microsecond-scale busy wait
 * first, because most of these resolve immediately and a full IOSleep(1)
 * would dominate; only then fall back to sleeping so a slow answer does not
 * burn a CPU.
 */
bool
BCM2837DWC2::waitForAnyBits(uint32_t offset, uint32_t mask, uint32_t timeoutMS)
{
	const uint32_t spinIterations = 1000; // 1000 x 1us

	for (uint32_t i = 0; i < spinIterations; i++) {
		if ((regRead(offset) & mask) != 0) {
			return true;
		}
		IODelay(1);
	}

	for (uint32_t ms = 0; ms < timeoutMS; ms++) {
		if ((regRead(offset) & mask) != 0) {
			return true;
		}
		IOSleep(1);
	}

	return false;
}

/*
 * Primary interrupt handler (DAR-136), same role/shape as
 * BCM2837SDHCI::interruptOccurred() - runs at hardware interrupt context,
 * so it may only touch fStatusLock (a real IOSimpleLock/spinlock) and call
 * IOLockWakeup(), which xnu's IOLocks.cpp implements as a bare
 * thread_wakeup_prim() that never itself touches fInterruptLock. No IOLog,
 * no IOLockLock/IOLockSleep - both documented in IOKit/IOLocks.h as unsafe
 * at interrupt level.
 *
 * Scans HAINT for every channel (DAR-370). Channel 0 is the synchronous
 * path, and channels 1..fAsyncChannelCount carry parked async IN
 * transfers. For those, the handler also pokes the UIM's
 * IOInterruptEventSource. With no provider, interruptOccurred() goes
 * straight to normalInterruptOccurred() (third_party/xnu-7195/iokit/Kernel/
 * IOInterruptEventSource.cpp:520-528). That only bumps producerCount,
 * records statistics and calls signalWorkAvailable() (:460-486), the same
 * work it does when a provider's interrupt controller calls it at primary
 * interrupt level, so it is safe to call here.
 */
void
BCM2837DWC2::interruptOccurred(OSObject *target, void * /*refCon*/, IOService * /*nub*/, int /*source*/)
{
	BCM2837DWC2 *self = OSDynamicCast(BCM2837DWC2, target);
	if (self == NULL) {
		return;
	}

	uint32_t gintsts = self->regRead(kDWC2_GINTSTS);
	if (!(gintsts & kGINTSTS_HCHINT)) {
		return;
	}

	uint32_t haint = self->regRead(kDWC2_HAINT) & self->regRead(kDWC2_HAINTMSK);
	bool notifyAsync = false;

	for (uint32_t chan = 0; chan < self->fNumChannels && chan < kDWC2MaxChannels; chan++) {
		if (!(haint & (1U << chan))) {
			continue;
		}

		uint32_t hcint = self->regRead(self->hcOffset(chan, kDWC2_HCINT));
		if (hcint == 0) {
			continue;
		}

		// Ack (W1C) exactly what was read, same reasoning as
		// BCM2837SDHCI::interruptOccurred(): a blind full-register write
		// risks clobbering a bit that arrived in the instant between the
		// read above and this write.
		self->regWrite(self->hcOffset(chan, kDWC2_HCINT), hcint);

		IOInterruptState istate = IOSimpleLockLockDisableInterrupt(self->fStatusLock);
		self->fPendingHcInt[chan] |= hcint;
		self->fRealInterruptHits++;
		IOSimpleLockUnlockEnableInterrupt(self->fStatusLock, istate);

		IOLockWakeup(self->fInterruptLock, (void *)&self->fPendingHcInt[chan], false);

		if (self->fAsyncBusy[chan]) {
			self->fAsyncInterrupts++;
			notifyAsync = true;
		}
	}

	IOInterruptEventSource *notify = self->fAsyncNotify;
	if (notifyAsync && notify != NULL) {
		notify->interruptOccurred(NULL, NULL, 0);
	}
}

/*
 * Real interrupt/poll hybrid wait for the single shared hardware channel,
 * modeled directly on BCM2837SDHCI::waitForIntStatus32() (see that
 * function's own comment for the two-lock rationale). Differs from
 * SDHCI's version in what it reports: this driver's own caller
 * (transferChunk(), via waitForChannelSettled()) needs the FULL live HCINT
 * bit pattern observed at match time, not just the bits in `mask` - the
 * same DMA completion that sets CHHLTD/NAK can carry XFERCOMP/STALL/error
 * bits the caller decodes separately (see transferChunk()/doTransfer()).
 * fPendingHcInt is therefore accumulated by OR (never filtered to `mask`)
 * and only cleared once a `mask` bit is actually seen in it, or on
 * timeout - never partially, so no bit set by a real interrupt or a poll
 * read is ever silently dropped before some call gets to see it.
 */
bool
BCM2837DWC2::waitForChannelInt(uint32_t chan, uint32_t mask, uint32_t timeoutMS, uint32_t *hcintOut)
{
	IOLockLock(fInterruptLock);
	bool matched = false;
	uint32_t observed = 0;
	uint32_t waitedMS = 0;
	const uint32_t kPollIntervalMS = 1;

	for (;;) {
		IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
		if ((fPendingHcInt[chan] & mask) != 0) {
			matched = true;
			observed = fPendingHcInt[chan];
			fPendingHcInt[chan] = 0;
		}
		IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

		if (matched) {
			break;
		}

		// Poll fallback: read+ack the raw register ourselves, same bit
		// handling as interruptOccurred(). A real interrupt for the same
		// event may race this read - harmless, since ack is idempotent
		// (W1C) and both paths take fStatusLock before touching
		// fPendingHcInt[chan].
		uint32_t raw = regRead(hcOffset(chan, kDWC2_HCINT));
		if (raw != 0) {
			regWrite(hcOffset(chan, kDWC2_HCINT), raw);
			istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
			fPendingHcInt[chan] |= raw;
			fPollFallbackHits++;
			if ((fPendingHcInt[chan] & mask) != 0) {
				matched = true;
				observed = fPendingHcInt[chan];
				fPendingHcInt[chan] = 0;
			}
			IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);
			if (matched) {
				break;
			}
		}

		if (waitedMS >= timeoutMS) {
			// Timeout: report whatever is live right now, matching the
			// pre-interrupt polled code's own unconditional final
			// regRead() so the caller's own timeout diagnostic still
			// prints something meaningful.
			observed = regRead(hcOffset(chan, kDWC2_HCINT));
			break;
		}

		uint64_t pollDeadline;
		clock_interval_to_deadline(kPollIntervalMS, kMillisecondScale, &pollDeadline);
		IOLockSleepDeadline(fInterruptLock, (void *)&fPendingHcInt[chan], pollDeadline, THREAD_UNINT);
		waitedMS += kPollIntervalMS;
	}
	IOLockUnlock(fInterruptLock);

	if (!fLoggedRealInterrupt && fRealInterruptHits > 0) {
		fLoggedRealInterrupt = true;
		IOLog("BCM2837DWC2: real hardware interrupt observed at least once "
		    "(interruptOccurred() -> waitForChannelInt())\n");
	}
	if (!fLoggedPollFallback && fPollFallbackHits > 0) {
		fLoggedPollFallback = true;
		IOLog("BCM2837DWC2: poll fallback resolved at least one wait "
		    "(no interrupt arrived within one %u ms tick)\n", kPollIntervalMS);
	}

	if (hcintOut != NULL) {
		*hcintOut = observed;
	}
	return matched;
}

#pragma mark - Core bring-up

bool
BCM2837DWC2::flushTxFIFO(uint32_t num)
{
	regWrite(kDWC2_GRSTCTL, kGRSTCTL_TXFFLSH | (num << kGRSTCTL_TXFNUM_SHIFT));
	if (!waitForBits(kDWC2_GRSTCTL, kGRSTCTL_TXFFLSH, false, 1000)) {
		IOLog("BCM2837DWC2: TX FIFO flush timeout\n");
		return false;
	}
	IODelay(1); // 3 PHY clocks, per u-boot
	return true;
}

bool
BCM2837DWC2::flushRxFIFO(void)
{
	regWrite(kDWC2_GRSTCTL, kGRSTCTL_RXFFLSH);
	if (!waitForBits(kDWC2_GRSTCTL, kGRSTCTL_RXFFLSH, false, 1000)) {
		IOLog("BCM2837DWC2: RX FIFO flush timeout\n");
		return false;
	}
	IODelay(1);
	return true;
}

bool
BCM2837DWC2::coreReset(void)
{
	if (!waitForBits(kDWC2_GRSTCTL, kGRSTCTL_AHBIDLE, true, 1000)) {
		IOLog("BCM2837DWC2: AHB idle timeout before core reset\n");
		return false;
	}

	regWrite(kDWC2_GRSTCTL, kGRSTCTL_CSFTRST);
	if (!waitForBits(kDWC2_GRSTCTL, kGRSTCTL_CSFTRST, false, 1000)) {
		IOLog("BCM2837DWC2: core soft reset timeout\n");
		return false;
	}

	/*
	 * u-boot's comment on this delay is emphatic and worth preserving:
	 * "This long sleep is _very_ important, otherwise the core will not
	 * stay in host mode after a connector ID change". Not a guess-margin;
	 * dropping it produces a core that silently falls back to device mode.
	 */
	IOSleep(100);
	return true;
}

bool
BCM2837DWC2::coreInit(void)
{
	uint32_t usbcfg = regRead(kDWC2_GUSBCFG);

	// This board's PHY is not ULPI with external VBUS control, and does not
	// use TS Dline pulsing.
	usbcfg &= ~kGUSBCFG_ULPI_EXT_VBUS_DRV;
	usbcfg &= ~kGUSBCFG_TERM_SEL_DL_PULSE;
	regWrite(kDWC2_GUSBCFG, usbcfg);

	if (!coreReset()) {
		return false;
	}

	/*
	 * High-speed PHY setup. These bits are preserved across a soft reset,
	 * so they are programmed once and then the core is reset again to
	 * latch them - the order matters and is u-boot's.
	 *
	 * BCM2837's PHY is UTMI+ at 8 bits wide (u-boot's defaults for this
	 * core: CONFIG_DWC2_PHY_TYPE = DWC2_PHY_TYPE_UTMI, CONFIG_DWC2_UTMI_WIDTH
	 * = 8), so ULPI_UTMI_SEL is cleared to select UTMI+ and PHYIF is left
	 * clear for the 8-bit width.
	 */
	usbcfg = regRead(kDWC2_GUSBCFG);
	usbcfg &= ~(kGUSBCFG_ULPI_UTMI_SEL | kGUSBCFG_PHYIF);
	regWrite(kDWC2_GUSBCFG, usbcfg);

	if (!coreReset()) {
		return false;
	}

	usbcfg = regRead(kDWC2_GUSBCFG);
	usbcfg &= ~(kGUSBCFG_ULPI_FSLS | kGUSBCFG_ULPI_CLK_SUS_M);
	/*
	 * Force host mode. The Pi 3 wires this OTG-capable core as a pure host
	 * (the LAN9514 is soldered to it, there is no peripheral role to
	 * negotiate), and u-boot sets FORCEHOSTMODE for the same reason via
	 * its hnp_srp_disable path, which the rpi DT selects. Without it the
	 * core can sit in device mode and no amount of host-register
	 * programming will produce a SOF.
	 */
	usbcfg |= kGUSBCFG_FORCEHOSTMODE;
	regWrite(kDWC2_GUSBCFG, usbcfg);
	// Mode change needs time to take effect before host registers mean
	// anything (u-boot gets this for free from its own reset delays).
	IOSleep(50);

	// AHB configuration: burst length and DMA enable, chosen by what the
	// core reports it is (GHWCFG2.ARCHITECTURE), not assumed.
	uint32_t ghwcfg2 = regRead(kDWC2_GHWCFG2);
	uint32_t arch = (ghwcfg2 & kHWCFG2_ARCHITECTURE_MASK) >> kHWCFG2_ARCHITECTURE_SHIFT;
	uint32_t ahbcfg = 0;

	switch (arch) {
	case kHWCFG2_ARCH_INT_DMA:
		ahbcfg |= kGAHBCFG_HBURSTLEN_INCR4 | kGAHBCFG_DMAENABLE;
		break;
	case kHWCFG2_ARCH_EXT_DMA:
		ahbcfg |= kGAHBCFG_HBURSTLEN_INCR4 | kGAHBCFG_DMAENABLE;
		break;
	case kHWCFG2_ARCH_SLAVE_ONLY:
	default:
		/*
		 * Slave-only means no bus mastering: every byte would have to be
		 * pushed through the FIFO registers by the CPU. This driver is
		 * DMA-only, so bail loudly rather than appearing to work and
		 * then transferring garbage. Real BCM2837 silicon reports
		 * internal DMA, as does the QEMU model, so reaching this means
		 * something is genuinely wrong about what we are talking to.
		 */
		IOLog("BCM2837DWC2: core reports slave-only architecture (%u) - "
		      "this driver requires DMA\n", arch);
		return false;
	}
	regWrite(kDWC2_GAHBCFG, ahbcfg);

	/*
	 * Real interrupt plumbing (DAR-136): unmask the two upper levels of
	 * the real 3-level status/mask hierarchy (HCINTMSK -> HAINTMSK ->
	 * GINTMSK -> GAHBCFG.GLBLINTRMSK, confirmed against QEMU's own real
	 * dwc2_update_irq()/dwc2_raise_host_irq() in
	 * third_party/qemu-zhuowei/hw/usb/hcd-dwc2.c, which gates the actual
	 * qemu_set_irq() on exactly this chain) here, safely: GLBLINTRMSK
	 * (the master gate, set separately in start() only after the real
	 * interrupt handler is registered) is still clear at this point, so
	 * none of this can actually assert the interrupt line yet.
	 * HCINTMSK itself (the lowest level, per-channel) is armed/disarmed
	 * per-transfer around doTransfer()'s own HCINT-clear, not here.
	 */
	regWrite(kDWC2_GINTMSK, kGINTSTS_HCHINT);
	// Channel 0 only. Async channels add their own bit while they are
	// parked (asyncStart()/asyncRelease()).
	regWrite(kDWC2_HAINTMSK, 1U << kDWC2Channel);

	// No HNP/SRP: forced host, nothing to negotiate.
	regModify(kDWC2_GUSBCFG, kGUSBCFG_HNPCAP | kGUSBCFG_SRPCAP, 0);

	fNumChannels = ((ghwcfg2 & kHWCFG2_NUM_HOST_CHAN_MASK) >>
	                kHWCFG2_NUM_HOST_CHAN_SHIFT) + 1;

	return true;
}

bool
BCM2837DWC2::hostInit(void)
{
	// Restart the PHY clock.
	regWrite(kDWC2_PCGCCTL, 0);

	/*
	 * FS/LS PHY clock select. With a high-speed PHY running at high or
	 * full speed this is the 30/60MHz setting; the 48MHz setting is only
	 * for a dedicated full-speed PHY, which this board does not have.
	 */
	regModify(kDWC2_HCFG, kHCFG_FSLSPCLKSEL_MASK, kHCFG_FSLSPCLKSEL_30_60_MHZ);

	// FIFO sizing, only if the core actually has a dynamic FIFO.
	if (regRead(kDWC2_GHWCFG2) & kHWCFG2_DYNAMIC_FIFO) {
		regWrite(kDWC2_GRXFSIZ, kDWC2RxFIFOSize);
		regWrite(kDWC2_GNPTXFSIZ,
		    (kDWC2NPTxFIFOSize << kFIFOSIZE_DEPTH_SHIFT) |
		    (kDWC2RxFIFOSize << kFIFOSIZE_STARTADDR_SHIFT));
		regWrite(kDWC2_HPTXFSIZ,
		    (kDWC2PTxFIFOSize << kFIFOSIZE_DEPTH_SHIFT) |
		    ((kDWC2RxFIFOSize + kDWC2NPTxFIFOSize) << kFIFOSIZE_STARTADDR_SHIFT));
	}

	regModify(kDWC2_GOTGCTL, kGOTGCTL_HSTSETHNPEN, 0);

	if (!flushTxFIFO(0x10)) { // 0x10 = all TX FIFOs
		return false;
	}
	if (!flushRxFIFO()) {
		return false;
	}

	// Put every channel into a known halted state. Two passes, as u-boot
	// does: first clear enable/direction and request disable, then assert
	// enable+disable together (the silicon's way of forcing a halt) and
	// wait for the enable bit to drop.
	for (uint32_t i = 0; i < fNumChannels; i++) {
		regModify(hcOffset(i, kDWC2_HCCHAR),
		    kHCCHAR_CHEN | kHCCHAR_EPDIR, kHCCHAR_CHDIS);
	}
	for (uint32_t i = 0; i < fNumChannels; i++) {
		regModify(hcOffset(i, kDWC2_HCCHAR),
		    kHCCHAR_EPDIR, kHCCHAR_CHEN | kHCCHAR_CHDIS);
		if (!waitForBits(hcOffset(i, kDWC2_HCCHAR), kHCCHAR_CHEN, false, 1000)) {
			IOLog("BCM2837DWC2: channel %u halt timeout during host init\n", i);
			// Not fatal on its own - a wedged spare channel does not
			// stop channel 0 working - so keep going rather than
			// failing the whole controller.
		}
	}

	return true;
}

#pragma mark - Root port

/*
 * HPRT0 carries four write-1-to-clear change/status bits mixed in with the
 * control bits. A naive read-modify-write of this register acknowledges
 * whichever of them happened to be set, losing the event. Every write goes
 * through here so that cannot happen by accident.
 */
void
BCM2837DWC2::portWriteHPRT0(uint32_t clearBits, uint32_t setBits)
{
	uint32_t hprt0 = regRead(kDWC2_HPRT0);
	hprt0 &= ~kHPRT0_W1C_MASK;
	hprt0 &= ~clearBits;
	hprt0 |= setBits;
	regWrite(kDWC2_HPRT0, hprt0);
}

bool
BCM2837DWC2::portPowerOn(void)
{
	if (!(regRead(kDWC2_GINTSTS) & kGINTSTS_CURMODE_HOST)) {
		IOLog("BCM2837DWC2: core is not in host mode (GINTSTS=%#x)\n",
		    regRead(kDWC2_GINTSTS));
		return false;
	}

	if (!(regRead(kDWC2_HPRT0) & kHPRT0_PRTPWR)) {
		portWriteHPRT0(0, kHPRT0_PRTPWR);
	}

	// USB 2.0 spec 7.2.4.1: 100ms for a port's power to become good.
	IOSleep(100);
	return true;
}

bool
BCM2837DWC2::portWaitForConnect(uint32_t timeoutMS)
{
	for (uint32_t ms = 0; ms < timeoutMS; ms++) {
		if (regRead(kDWC2_HPRT0) & kHPRT0_PRTCONNSTS) {
			// Debounce, per USB 2.0 spec 7.1.7.3 (TATTDB, 100ms).
			IOSleep(100);
			return (regRead(kDWC2_HPRT0) & kHPRT0_PRTCONNSTS) != 0;
		}
		IOSleep(1);
	}
	return false;
}

bool
BCM2837DWC2::portReset(void)
{
	portWriteHPRT0(0, kHPRT0_PRTRST);
	// USB 2.0 spec 7.1.7.5: drive reset for at least 10ms. 60ms is what
	// Linux's dwc2 uses for this core, which is known to need the longer
	// end of the range.
	IOSleep(60);
	portWriteHPRT0(kHPRT0_PRTRST, 0);
	// Recovery time before the port is usable (spec: 10ms).
	IOSleep(30);

	if (!waitForBits(kDWC2_HPRT0, kHPRT0_PRTENA, true, 500)) {
		IOLog("BCM2837DWC2: port did not enable after reset (HPRT0=%#x)\n",
		    regRead(kDWC2_HPRT0));
		return false;
	}

	uint32_t hprt0 = regRead(kDWC2_HPRT0);
	fRootSpeed = (hprt0 & kHPRT0_PRTSPD_MASK) >> kHPRT0_PRTSPD_SHIFT;
	fRootConnected = true;

	/*
	 * HFIR is deliberately NOT written here, and the reason is narrower and
	 * more surprising than it looks - so it is worth stating precisely,
	 * because the obvious reading of this fix is the wrong one.
	 *
	 * This line used to be
	 * `regWrite(kDWC2_HFIR, (fRootSpeed == kUSBSpeedHigh) ? 60000 : 48000)`.
	 * With it, enumerating the LAN9514's downstream Ethernet function on a
	 * real Pi 3 failed deterministically: every control transfer to the
	 * device behind hub port 1 died with XACTERR, byte-for-byte identical
	 * register state on all three retries, while the hub itself at address 1
	 * enumerated perfectly over the same channel with identical HCCHAR
	 * programming. Deleting the write fixed it outright - the Ethernet
	 * function (0424:ec00) now enumerates.
	 *
	 * The value was NOT the problem. With the write gone, HFIR reads back
	 * 60000 on this board at high speed (see the IOLog below, which exists
	 * to keep that measurement in every boot log) - exactly what the old
	 * code was writing into it. Identical byte, opposite outcome. So the
	 * defect was the *act of writing* HFIR at this moment, not what was
	 * written: this runs immediately after the root port has come up and
	 * been enabled, and a plain write to HFIR reloads the frame counter
	 * mid-operation. The databook only sanctions reprogramming the frame
	 * interval on a running port through HFIR.HFIRRldCtrl; a raw write
	 * restarts the microframe cadence under a bus that is already running.
	 * The directly-attached hub tolerated that glitch; a device one hop
	 * further downstream, whose transactions the hub has to relay in the
	 * frame the host schedules them in, did not.
	 *
	 * (Do not "restore" this write with a corrected constant. An earlier
	 * version of this comment argued from the databook's PHY-clock formula
	 * that high speed on a 60MHz UTMI+ PHY should be 7500 and that 60000
	 * was an 8x-too-long microframe. The hardware says otherwise - this
	 * core sits at 60000 at high speed of its own accord and works. The
	 * formula-based reasoning was never measured; the 60000 above was.)
	 *
	 * Writing nothing also matches both reference drivers. u-boot's dwc2.c -
	 * the driver this one was ported from, and the one that drives this
	 * exact silicon in this project's own boot chain - never writes HFIR at
	 * all: it defines DWC2_HFIR_FRINT_MASK and never uses it. Linux's dwc2
	 * writes it only on the full/low-speed path in dwc2_hprt0_enable(), and
	 * only under params.reload_ctl - i.e. only where the reload control bit
	 * makes it legal.
	 *
	 * This could only ever have been caught on hardware. QEMU's dwc2 model
	 * resets hfir to 60000 and its HFIR write handler is a bare `break` -
	 * the value is stored and never read for any scheduling decision
	 * (third_party/qemu-zhuowei/hw/usb/hcd-dwc2.c). The QEMU boot that
	 * verified the rest of this driver could not have exercised this line in
	 * either branch, which is also why the full-speed branch's 48000 - a
	 * genuinely different value from the register's contents, unlike the
	 * high-speed branch's - went unnoticed there too.
	 */

	IOLog("BCM2837DWC2: root port enumerated: HPRT0=%#x HFIR=%u (core's own "
	      "value, not overridden) HCFG=%#x\n",
	    hprt0, regRead(kDWC2_HFIR), regRead(kDWC2_HCFG));

	return true;
}

#pragma mark - DMA buffer

bool
BCM2837DWC2::allocateDMABuffer(void)
{
	/*
	 * Physically contiguous (the core walks it as a flat bus address, it
	 * has no scatter/gather here) and uncached (so no explicit cache
	 * maintenance is needed around every transfer - u-boot instead does
	 * flush_dcache_range/invalidate_dcache_range by hand on a cached
	 * buffer; uncached is the simpler and harder-to-get-wrong of the two,
	 * and this buffer is nowhere near hot enough for the difference to
	 * matter).
	 *
	 * kIOMapWriteCombineCache, not kIOMapInhibitCache: on ARM64 the
	 * latter maps VM_WIMG_IO (MAIR_DISABLE, Device-nGnRnE), which
	 * unconditionally faults on any unaligned multi-byte access
	 * regardless of SCTLR.A. doTransfer()'s memcpy() in/out of this
	 * buffer runs at arbitrary transfer lengths (e.g. real Ethernet
	 * frame sizes), essentially never aligned, and reliably took an
	 * "Unaligned kernel data abort" panic on real RX traffic.
	 * kIOMapWriteCombineCache maps VM_WIMG_WCOMB (MAIR_WRITECOMB, real
	 * Normal Non-Cacheable memory), which XNU's SCTLR_EL1 config (no
	 * SCTLR_A) permits unaligned access on -- matching what this
	 * comment already assumed ("uncached") without actually being the
	 * memory type that promise requires.
	 *
	 * kIOMemoryMapperNone keeps any IOMapper out of the picture so
	 * getPhysicalSegment() returns a true physical address rather than a
	 * remapped DMA one; this platform has no IOMMU, and the bus address
	 * conversion below assumes physical.
	 *
	 * The mask bounds the allocation to the low 1GB - all the DRAM this
	 * board has, and all that the 0xc0000000 bus alias can reach.
	 */
	if (!allocateContiguousDMA(kDWC2DataBufferSize, "DMA buffer", &fDMABuffer,
	    &fDMAVirt, &fDMABusAddr)) {
		return false;
	}

	IOLog("BCM2837DWC2: DMA buffer virt=%p bus=%#llx len=%u\n",
	    fDMAVirt, fDMABusAddr, kDWC2DataBufferSize);

	return true;
}

/*
 * The allocation recipe allocateDMABuffer()'s comment explains, for any
 * size. On failure nothing is left allocated.
 */
bool
BCM2837DWC2::allocateContiguousDMA(uint32_t size, const char *what,
    IOBufferMemoryDescriptor **bmdOut, void **virtOut, uint64_t *busOut)
{
	IOBufferMemoryDescriptor *bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
	    kernel_task,
	    kIODirectionInOut | kIOMemoryPhysicallyContiguous |
	    kIOMapWriteCombineCache | kIOMemoryMapperNone,
	    size,
	    0x3FFFFFFFULL & ~0x3ULL); // 4-byte aligned, DWC2 requires it
	if (bmd == NULL) {
		IOLog("BCM2837DWC2: %s allocation failed\n", what);
		return false;
	}
	if (bmd->prepare() != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: %s prepare() failed\n", what);
		bmd->release();
		return false;
	}

	IOByteCount segLen = 0;
	IOPhysicalAddress phys = bmd->getPhysicalSegment(0, &segLen, 0);
	if (phys == 0 || segLen < size) {
		IOLog("BCM2837DWC2: %s not usable (phys=%#llx seglen=%llu)\n",
		    what, (uint64_t)phys, (uint64_t)segLen);
		bmd->complete();
		bmd->release();
		return false;
	}

	*bmdOut = bmd;
	*virtOut = bmd->getBytesNoCopy();
	*busOut = kBCM2837BusAddressBase | (uint64_t)phys;
	return true;
}

// Bytes an IN channel has received so far, from HCTSIZ.XFERSIZE counting
// down from what it was armed with.
uint32_t
BCM2837DWC2::channelInBytesDone(uint32_t chan, uint32_t xferLen) const
{
	uint32_t remaining = regRead(hcOffset(chan, kDWC2_HCTSIZ)) & kHCTSIZ_XFERSIZE_MASK;
	return (remaining <= xferLen) ? (xferLen - remaining) : 0;
}

// HCTSIZ.PID: the data toggle the core expects next on this channel.
uint8_t
BCM2837DWC2::channelNextPid(uint32_t chan) const
{
	return (uint8_t)((regRead(hcOffset(chan, kDWC2_HCTSIZ)) & kHCTSIZ_PID_MASK) >>
	    kHCTSIZ_PID_SHIFT);
}

#pragma mark - Transfer engine

void
BCM2837DWC2::channelInit(uint32_t chan, DWC2Endpoint *ep, bool isIn, bool useSplit)
{
	uint32_t hcchar =
	    ((uint32_t)ep->devAddr << kHCCHAR_DEVADDR_SHIFT) |
	    ((uint32_t)ep->epNum << kHCCHAR_EPNUM_SHIFT) |
	    (isIn ? kHCCHAR_EPDIR : 0) |
	    ((uint32_t)ep->epType << kHCCHAR_EPTYPE_SHIFT) |
	    ((uint32_t)ep->maxPacket & kHCCHAR_MPS_MASK);

	if (ep->speed == kUSBSpeedLow) {
		hcchar |= kHCCHAR_LSPDDEV;
	}

	regWrite(hcOffset(chan, kDWC2_HCCHAR), hcchar);

	/*
	 * Split transactions. A full/low-speed device behind a high-speed hub
	 * cannot be addressed directly at 480Mbit/s; the host issues SSPLIT to
	 * the hub, which relays at the device's speed, then CSPLIT to collect
	 * the result. On this board that is not an edge case - it is the only
	 * path to the LAN9514's Ethernet function.
	 *
	 * `useSplit` is decided once by doTransfer and passed in rather than
	 * re-derived from ep->splitHubAddr here. Deriving the same condition
	 * in two places let them disagree: HCSPLT could be armed with SPLTENA
	 * while doTransfer's SSPLIT/CSPLIT state machine stayed switched off,
	 * which issues split tokens and then never sends the matching CSPLIT.
	 */
	if (useSplit) {
		regWrite(hcOffset(chan, kDWC2_HCSPLT),
		    kHCSPLT_SPLTENA |
		    ((uint32_t)ep->splitHubAddr << kHCSPLT_HUBADDR_SHIFT) |
		    ((uint32_t)ep->splitHubPort << kHCSPLT_PRTADDR_SHIFT));
	} else {
		regWrite(hcOffset(chan, kDWC2_HCSPLT), 0);
	}
}

/*
 * Wait until the channel reports something worth acting on: it halted, or it
 * NAKed without halting.
 *
 * A halt is the ordinary end of a transfer. The second case is specific to
 * NON-PERIODIC (control/bulk) endpoints in buffer-DMA mode: a NAK does not
 * halt the channel. The core leaves it enabled and re-issues the token by
 * itself, raising only HCINT.NAK to say so. QEMU's dwc2 model implements the
 * same behaviour, and says so in as many words - "for ctrl/bulk,
 * automatically retry on NAK, but send the interrupt anyway"
 * (third_party/qemu-zhuowei/hw/usb/hcd-dwc2.c). Periodic endpoints DO halt on
 * a NAK, so they still come back through the CHHLTD path unchanged.
 *
 * Waiting for CHHLTD on a NAKed bulk IN therefore means waiting for the
 * device to have data, which for an idle read is never. That is not merely
 * slow: this driver runs every endpoint over ONE shared hardware channel, so
 * a channel parked with CHENA still set blocks every other endpoint as well.
 * Worse, it does so invisibly - re-arming a channel whose CHENA never dropped
 * is not an enable edge, so the next endpoint's transfer is programmed into
 * registers nothing ever acts on, while the controller keeps re-polling the
 * previous endpoint. That is exactly how the CDC-ECM interrupt (link-status)
 * endpoint came to be serviced by the UIM every tick without a single packet
 * ever reaching the device (DAR-282).
 */
bool
BCM2837DWC2::waitForChannelSettled(uint32_t chan, uint32_t timeoutMS, uint32_t *hcintOut)
{
	return waitForChannelInt(chan, kHCINT_CHHLTD | kHCINT_NAK, timeoutMS, hcintOut);
}

/*
 * Force a still-enabled channel to halt and report what it ended up saying.
 *
 * The two-bit CHENA+CHDIS write is the silicon's documented way of aborting a
 * channel, and is the same sequence hostInit() already uses to bring every
 * channel to a known state at startup.
 *
 * HCINT is re-read *after* the halt completes, deliberately: a transfer can
 * finish in the moment between deciding to abort it and the abort landing, and
 * the caller must see that XFERCOMP rather than throw away data the device
 * really did deliver.
 *
 * DAR-352 (real hardware, reproduced twice): the single 50ms wait this
 * function used to make was confirmed genuinely insufficient at least
 * twice on real silicon during completely ordinary idle RX polling, no
 * unusual load involved - HCCHAR read back with BOTH CHENA and CHDIS
 * still set, permanently. DAR-290's own comment already called this
 * "unverified... no retry/reset strategy is attempted"; it is verified
 * now. Retries the forced-disable request up to kHaltRetries times with
 * a longer wait each time (a real, transient miss on one attempt is a
 * different, more forgivable failure than the request never landing at
 * all) before treating it as a confirmed, permanent wedge - at which
 * point continuing to touch this channel is actively harmful (the next
 * doTransfer()'s CHEN-set write would OR onto an already-set bit instead
 * of a real disable->enable edge, servicing stale channel state forever
 * - DAR-282's exact class), so fChannelWedged is set instead. Every
 * caller reaching this channel again funnels through doTransfer()'s own
 * fChannelWedged check (see that function), so this is the only place
 * that needs to notice and record the wedge.
 *
 * This retries the halt. A confirmed channel-0 wedge here schedules
 * recoverController() (core reset + host re-init + root-port re-bring-up +
 * bus re-enumeration, DAR-352); until that runs, the board would otherwise
 * have needed a physical
 * power cycle after this fires. What this DOES fix: without it, every
 * later transfer attempt (chiefly the RX poll timer, running forever)
 * silently re-discovers the same stuck state every timeout period,
 * forever, flooding the console - real, observed behavior that on its
 * own made the board's serial console unusable, independent of whatever
 * else the wedge broke.
 */
void
BCM2837DWC2::haltChannel(uint32_t chan, uint32_t *hcintOut)
{
	const uint32_t kHaltRetries = 3;
	const uint32_t kHaltWaitMS[kHaltRetries] = { 50, 150, 400 };

	uint32_t hcint = 0;
	if (regRead(hcOffset(chan, kDWC2_HCCHAR)) & kHCCHAR_CHEN) {
		bool halted = false;
		for (uint32_t attempt = 0; attempt < kHaltRetries; attempt++) {
			// Re-issue the CHEN+CHDIS request on every attempt, not just
			// the first: if the request itself was what got lost (rather
			// than the channel genuinely taking longer than 50ms to
			// react to a request it did receive), waiting longer on the
			// same stale write would never help.
			regModify(hcOffset(chan, kDWC2_HCCHAR), 0,
			    kHCCHAR_CHEN | kHCCHAR_CHDIS);
			// Routed through the same interrupt/poll hybrid as
			// waitForChannelInt() (DAR-136), not a raw register poll:
			// HCINTMSK is still armed for this channel while haltChannel()
			// runs (doTransfer only disarms it after this function
			// returns), so a real interrupt can land here too, and an
			// unsynchronized second poller racing interruptOccurred()'s
			// own read-ack-fStatusLock sequence is exactly the class of
			// bug fTransferLock's own header comment documents (DAR-282).
			bool sawHalt = waitForChannelInt(chan, kHCINT_CHHLTD, kHaltWaitMS[attempt], &hcint);
			if (sawHalt || !(regRead(hcOffset(chan, kDWC2_HCCHAR)) & kHCCHAR_CHEN)) {
				halted = true;
				break;
			}
			IOLog("BCM2837DWC2: haltChannel: forced halt did not complete within "
			      "%ums (attempt %u/%u), channel %u still enabled (HCCHAR=%#x HCINT=%#x)\n",
			    kHaltWaitMS[attempt], attempt + 1, kHaltRetries, chan,
			    regRead(hcOffset(chan, kDWC2_HCCHAR)), hcint);
		}
		if (!halted && chan != kDWC2Channel) {
			// A parked async channel (DAR-370). Retire just that channel;
			// channel 0 and the other async channels are unaffected.
			fAsyncWedged[chan] = true;
			IOLog("BCM2837DWC2: haltChannel: async channel %u CONFIRMED WEDGED "
			      "(HCCHAR=%#x) - retired, no further transfers will use it\n",
			    chan, regRead(hcOffset(chan, kDWC2_HCCHAR)));
		} else if (!halted) {
			fChannelWedged = true;
			scheduleRecovery();
			IOLog("BCM2837DWC2: haltChannel: channel %u CONFIRMED WEDGED after %u "
			      "retries (HCCHAR=%#x) - transfers fail immediately "
			      "(kIOReturnNotAttached) until controller recovery (DAR-352) "
			      "resets the core\n",
			    chan, kHaltRetries, regRead(hcOffset(chan, kDWC2_HCCHAR)));
		}
	} else {
		hcint = regRead(hcOffset(chan, kDWC2_HCINT));
	}
	if (hcintOut != NULL) {
		*hcintOut = hcint;
	}
}

/*
 * Wait out one armed chunk and decode what the silicon reports. doTransfer
 * has already programmed HCTSIZ/HCDMA and set CHEN; this only interprets the
 * result, so it deliberately takes no buffer or PID - those belong to the
 * caller, which owns the multi-chunk loop and the sequence bit.
 */
IOReturn
BCM2837DWC2::transferChunk(uint32_t xferLen, bool isIn, uint32_t *actualLen, uint32_t *hcintOut)
{
	*actualLen = 0;

	uint32_t hcint = 0;
	if (!waitForChannelSettled(kDWC2Channel, 2000, &hcint)) {
		IOLog("BCM2837DWC2: channel halt timeout (HCINT=%#x HCCHAR=%#x)\n",
		    hcint, regRead(hcOffset(kDWC2Channel, kDWC2_HCCHAR)));
		if (hcintOut != NULL) {
			*hcintOut = hcint;
		}
		return kIOReturnTimeout;
	}

	if (!(hcint & kHCINT_CHHLTD)) {
		// A NAK that did not halt the channel - see waitForChannelSettled.
		// The channel has to be taken down before the next transfer can be
		// armed on it, because only a real disabled->enabled edge starts
		// one.
		//
		// OR in haltChannel()'s own observed bits rather than replacing
		// hcint outright (DAR-136): waitForChannelInt() now acks (W1C)
		// whatever it reads the moment it is observed, so the NAK bit
		// just matched above is already cleared in the real register by
		// the time haltChannel() does its own read - a plain overwrite
		// here would lose it, misreporting a completely normal "device
		// had nothing ready, auto-retried" NAK as a bare CHHLTD, which
		// falls through every known-reason check below into the generic
		// "unknown" I/O error. This is exactly the trap the pre-interrupt
		// polled code never had to worry about, since nothing was ever
		// acked out from under it.
		uint32_t haltHcint = 0;
		haltChannel(kDWC2Channel, &haltHcint);
		hcint |= haltHcint;
	}

	if (hcintOut != NULL) {
		*hcintOut = hcint;
	}

	return decodeChannelResult(kDWC2Channel, hcint, xferLen, isIn, actualLen);
}

/*
 * Transfer-failure logging is rate limited: the console is polled serial on
 * a single CPU, and a device that fails every poll (an unplugged keyboard, a
 * dead TT) otherwise turns each 10 ms poll into a line that starves the
 * system. At most kFailLogBurst lines per second; the count of dropped ones
 * is reported when the next window opens. Unlocked on purpose - a lost
 * increment only miscounts a diagnostic.
 */
bool
BCM2837DWC2::failLogAllowed(void)
{
	static const uint32_t kFailLogBurst = 10;
	uint64_t now = mach_absolute_time();
	uint64_t window;
	nanoseconds_to_absolutetime(NSEC_PER_SEC, &window);
	if (fFailLogWindowStart == 0 || now - fFailLogWindowStart >= window) {
		if (fFailLogSuppressed != 0) {
			IOLog("BCM2837DWC2: %u transfer-failure message(s) suppressed\n",
			    fFailLogSuppressed);
		}
		fFailLogWindowStart = now;
		fFailLogCount = 0;
		fFailLogSuppressed = 0;
	}
	if (fFailLogCount < kFailLogBurst) {
		fFailLogCount++;
		return true;
	}
	fFailLogSuppressed++;
	return false;
}

/*
 * Decode what a halted channel reports. Split out of transferChunk() for
 * DAR-370 so the async path decodes a parked channel exactly the way the
 * synchronous path does.
 */
IOReturn
BCM2837DWC2::decodeChannelResult(uint32_t chan, uint32_t hcint, uint32_t xferLen,
    bool isIn, uint32_t *actualLen)
{
	*actualLen = 0;

	uint32_t hctsiz = regRead(hcOffset(chan, kDWC2_HCTSIZ));
	uint32_t remaining = hctsiz & kHCTSIZ_XFERSIZE_MASK;

	if (hcint & kHCINT_XFERCOMP) {
		/*
		 * The HCTSIZ.XFERSIZE remainder is only meaningful for an IN.
		 * On an IN the core counts it down as data arrives, so the delta
		 * is the real byte count and is how a short packet is detected.
		 * On an OUT it is not a transmitted-byte count, and the real
		 * reference driver deliberately does not use it: u-boot's
		 * chunk_msg() (third_party/u-boot/drivers/usb/host/dwc2.c:893-902)
		 * subtracts it under `if (in)` only, then reports
		 * `*actual_len = xfer_len` -- i.e. for an OUT that completed,
		 * the full requested length was sent.
		 *
		 * Applying the subtraction to OUT transfers too was a real bug
		 * (DAR-326), and a confusing one, because the frame really did
		 * go out: bulkOut() returned a short count, so
		 * SMSC9514Ethernet::outputPacket() reported
		 * kIOReturnOutputDropped, which is 0x0001
		 * (kIOOutputStatusDropped in iokit/IOKit/network/IOOutputQueue.h)
		 * -- and IONetworkingFamily hands that value straight up to the
		 * BSD stack as an errno, where 1 is EPERM. A real Pi 3 therefore
		 * answered every ping correctly while printing
		 * "ping: sendto: Operation not permitted" for each one.
		 */
		if (isIn) {
			*actualLen = channelInBytesDone(chan, xferLen);
		} else {
			*actualLen = xferLen;
		}
		return kIOReturnSuccess;
	}

	if (hcint & (kHCINT_NAK | kHCINT_FRMOVRUN | kHCINT_NYET)) {
		return kIOReturnNotReady; // caller retries; normal flow control
	}
	if (hcint & kHCINT_STALL) {
		return kIOReturnNotResponding; // endpoint stalled: real protocol error
	}

	/*
	 * A start-split the hub ACKed halts with CHHLTD|ACK and no XFERCOMP.
	 * That is the hub accepting the job, not a failure: doTransfer()'s
	 * split state machine sees the ACK and issues the CSPLIT, exactly as
	 * u-boot's chunk_msg() does (third_party/u-boot/drivers/usb/host/
	 * dwc2.c, the `else if (do_split)` branch after transfer_chunk()).
	 * Return an error so no byte count is taken from it, but do not log:
	 * on real hardware every poll of a low-speed keyboard behind the Apple
	 * keyboard's hub hit this, and one serial line per 10 ms poll starved
	 * the single-CPU boot before launchd ran.
	 */
	uint32_t hcsplt = regRead(hcOffset(chan, kDWC2_HCSPLT));
	if ((hcsplt & kHCSPLT_SPLTENA) && !(hcsplt & kHCSPLT_COMPSPLT) &&
	    (hcint & kHCINT_ACK) &&
	    !(hcint & (kHCINT_XACTERR | kHCINT_BBLERR | kHCINT_AHBERR |
	    kHCINT_DATATGLERR))) {
		return kIOReturnIOError;
	}

	/*
	 * Everything else genuinely is an I/O error, but which one matters for
	 * diagnosis and kIOReturnIOError alone throws that away. Decode the
	 * remaining HCINT bits by name (AHBERR/BBLERR/XACTERR/DATATGLERR are
	 * mutually exclusive in practice - the silicon halts the channel on
	 * the first one it hits) plus the channel state that produced it, so
	 * a real-hardware failure is diagnosable from one boot capture instead
	 * of needing a second pass with more logging added after the fact.
	 */
	const char *reason = "unknown";
	if (hcint & kHCINT_XACTERR) {
		reason = "XACTERR (transaction error: bad PID/CRC/timeout at the PHY)";
	} else if (hcint & kHCINT_BBLERR) {
		reason = "BBLERR (babble: device sent more than expected)";
	} else if (hcint & kHCINT_AHBERR) {
		reason = "AHBERR (AHB bus error - DMA address/access problem)";
	} else if (hcint & kHCINT_DATATGLERR) {
		reason = "DATATGLERR (data toggle mismatch)";
	}
	if (failLogAllowed()) {
		IOLog("BCM2837DWC2: transfer failed on channel %u: %s (HCINT=%#x HCCHAR=%#x "
		      "HCSPLT=%#x HCTSIZ=%#x xferLen=%u)\n",
		    chan, reason, hcint, regRead(hcOffset(chan, kDWC2_HCCHAR)),
		    regRead(hcOffset(chan, kDWC2_HCSPLT)), hctsiz, xferLen);
	}

	return kIOReturnIOError;
}

IOReturn
BCM2837DWC2::doTransfer(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
                        void *buffer, uint32_t len, uint32_t *actualLen)
{
	if (fRegBase == NULL || fDMAVirt == NULL) {
		return kIOReturnNotReady;
	}
	// DAR-352: the single choke point every public entry point
	// (transfer()/controlTransfer()/transferStage(), all via
	// doTransferRetrying()) funnels through, all under fTransferLock -
	// see fChannelWedged's own header comment. kIOReturnNotAttached
	// (not kIOReturnNotReady) deliberately: doTransferRetrying()'s own
	// retry loop treats kIOReturnNotReady as "keep retrying" and
	// anything else as "return immediately" - reusing kIOReturnNotReady
	// here would silently retry against dead hardware for the caller's
	// full timeout instead of failing instantly.
	if (fChannelWedged) {
		return kIOReturnNotAttached;
	}
	if (ep->maxPacket == 0) {
		return kIOReturnBadArgument;
	}

	uint32_t done = 0;
	IOReturn ret = kIOReturnSuccess;
	bool stopTransfer = false;
	bool doSplit = false;
	bool completeSplit = false;
	uint32_t ssplitFrameNum = 0;

	*actualLen = 0;

	uint32_t maxXferLen = kDWC2MaxPacketCount * ep->maxPacket;
	if (maxXferLen > kDWC2MaxTransferSize) {
		maxXferLen = kDWC2MaxTransferSize;
	}
	if (maxXferLen > kDWC2DataBufferSize) {
		maxXferLen = kDWC2DataBufferSize;
	}
	// Keep whole packets only, so a chunk boundary never lands mid-packet.
	maxXferLen = (maxXferLen / ep->maxPacket) * ep->maxPacket;

	/*
	 * Decide whether this transfer needs SPLIT wrapping: a device slower
	 * than the bus it is reached over. ep->splitHubAddr being set is the
	 * caller telling us which hub to address; the root port speed tells us
	 * whether splitting is required at all (a full-speed device on a
	 * full-speed root port talks directly). Decided before channelInit so
	 * HCSPLT and the state machine below cannot disagree.
	 */
	if (ep->speed != kUSBSpeedHigh && ep->splitHubAddr != 0 &&
	    fRootSpeed == kUSBSpeedHigh) {
		doSplit = true;
		maxXferLen = ep->maxPacket;
	}

	channelInit(kDWC2Channel, ep, isIn, doSplit);

	do {
		uint32_t xferLen = len - done;
		uint32_t numPackets;

		if (xferLen > maxXferLen) {
			xferLen = maxXferLen;
		}
		if (xferLen > ep->maxPacket) {
			numPackets = (xferLen + ep->maxPacket - 1) / ep->maxPacket;
		} else {
			numPackets = 1;
		}
		if (doSplit) {
			numPackets = 1;
		}

		if (completeSplit) {
			regModify(hcOffset(kDWC2Channel, kDWC2_HCSPLT), 0, kHCSPLT_COMPSPLT);
		} else if (doSplit) {
			regModify(hcOffset(kDWC2Channel, kDWC2_HCSPLT), kHCSPLT_COMPSPLT, 0);
		}

		bool oddFrame = false;
		if (ep->epType == kHCCHAR_EPTYPE_INTR) {
			// Periodic transfers must be scheduled into the opposite
			// frame parity from the current one.
			oddFrame = ((regRead(kDWC2_HFNUM) & 1) == 0);
		}

		uint32_t actual = 0;
		uint32_t hcint = 0;

		/*
		 * XACTERR retry, bounded at 3 attempts total - the same Cerr=3
		 * convention the EHCI spec itself mandates in hardware (a qTD's
		 * error counter field), and which Linux's own EHCI driver added
		 * a software equivalent of specifically because real transaction
		 * errors on real silicon are often transient and "generally
		 * succeed the second or third time through" (its own commit
		 * message). Confirmed real, not a QEMU artifact: this exact path
		 * failed on real hardware enumerating the actual LAN9514 (device
		 * 0424:9514) on the very first SETUP token to a freshly-reset
		 * high-speed port - a moment particularly prone to real PHY-level
		 * transients while clocks/negotiation settle, and exactly the
		 * class of error this convention exists for. u-boot's own
		 * reference driver has zero special-case handling for XACTERR
		 * either (confirmed absent, not just unread) - it is a bring-up
		 * driver, not held to this standard; this stack now is.
		 *
		 * Scoped to XACTERR specifically, not the other fallback errors
		 * (BBLERR/AHBERR/DATATGLERR): those indicate a real protocol or
		 * bus problem, not the kind of line-level noise this convention
		 * targets, and retrying them blindly would mask a genuine bug
		 * instead of working around a transient one.
		 */
		for (uint32_t attempt = 0; attempt < 3; attempt++) {
			// Program the transfer descriptor.
			regWrite(hcOffset(kDWC2Channel, kDWC2_HCTSIZ),
			    (xferLen & kHCTSIZ_XFERSIZE_MASK) |
			    (numPackets << kHCTSIZ_PKTCNT_SHIFT) |
			    ((uint32_t)(*pid) << kHCTSIZ_PID_SHIFT));

			if (xferLen > 0 && !isIn) {
				memcpy(fDMAVirt, (uint8_t *)buffer + done, xferLen);
			}

			regWrite(hcOffset(kDWC2Channel, kDWC2_HCDMA), (uint32_t)fDMABusAddr);

			// Clear any stale conditions for this channel before arming it.
			regWrite(hcOffset(kDWC2Channel, kDWC2_HCINT), kHCINT_ALL);

			// Unmask (DAR-136): the lowest level of the real 3-level
			// HCINTMSK -> HAINTMSK -> GINTMSK -> GAHBCFG.GLBLINTRMSK
			// hierarchy (the upper three are set once in hostInit()/
			// start() and left armed). Symmetric disarm is at this
			// function's end (regWrite(..HCINTMSK.., 0)).
			regWrite(hcOffset(kDWC2Channel, kDWC2_HCINTMSK), kHCINT_ALL);

			/*
			 * Ordering matters here: the staging buffer write above and
			 * the HCDMA/HCTSIZ programming must all be visible to the
			 * DWC2 bus master before the channel is enabled. The buffer
			 * is Normal non-cacheable memory while the registers are
			 * Device memory, and ARM does not order those against each
			 * other, so an explicit barrier is required rather than
			 * implied.
			 */
			__asm__ volatile ("dsb sy" ::: "memory");

			regModify(hcOffset(kDWC2Channel, kDWC2_HCCHAR),
			    kHCCHAR_MULTICNT_MASK | kHCCHAR_CHEN | kHCCHAR_CHDIS | kHCCHAR_ODDFRM,
			    (1U << kHCCHAR_MULTICNT_SHIFT) |
			    (oddFrame ? kHCCHAR_ODDFRM : 0) |
			    kHCCHAR_CHEN);

			actual = 0;
			hcint = 0;
			ret = transferChunk(xferLen, isIn, &actual, &hcint);

			if (!(ret == kIOReturnIOError && (hcint & kHCINT_XACTERR))) {
				break;
			}
			if (attempt + 1 < 3 && failLogAllowed()) {
				IOLog("BCM2837DWC2: XACTERR on attempt %u/3, retrying\n",
				    attempt + 1);
			}
		}

		/*
		 * Out of retries, report it the way Apple's EHCI UIM does after its
		 * own Cerr=3 hardware retries (mungeEHCIStatus(),
		 * AppleUSBEHCI_UIM.cpp:1216-1225): kIOUSBHighSpeedSplitError for a
		 * split transaction, "not responding" otherwise. kIOReturnDeviceError
		 * stands for the latter because kIOReturnNotResponding already means
		 * STALL on this interface; the UIM translates it. Either way
		 * IOUSBHIDDriver::InterruptReadHandler() (IOUSBHIDDriver.cpp:1670-
		 * 1671) then runs its dead-device check instead of re-arming forever,
		 * which a generic kIOReturnIOError did - an unplugged keyboard
		 * flooded the console.
		 */
		if (ret == kIOReturnIOError && (hcint & kHCINT_XACTERR)) {
			ret = doSplit ? kIOUSBHighSpeedSplitError : kIOReturnDeviceError;
		}

		if (ret == kIOReturnSuccess && xferLen > 0 && isIn) {
			// Read back only what actually arrived.
			__asm__ volatile ("dsb sy" ::: "memory");
			memcpy((uint8_t *)buffer + done, fDMAVirt, actual);
		}

		/*
		 * Split-transaction state machine, following u-boot's chunk_msg.
		 * An SSPLIT that the hub ACKs is not the transfer completing - it
		 * is the hub accepting the job - so the next pass must issue the
		 * matching CSPLIT. A CSPLIT answered with NYET means "not ready
		 * yet, ask again", which is only an error if the hub keeps saying
		 * it for more than a few frames.
		 */
		if (completeSplit) {
			stopTransfer = false;
			if (hcint & kHCINT_NYET) {
				ret = kIOReturnSuccess;
				uint32_t frameNum = regRead(kDWC2_HFNUM) & kHFNUM_MAX_FRNUM;
				if (((frameNum - ssplitFrameNum) & kHFNUM_MAX_FRNUM) > 4) {
					ret = kIOReturnNotReady;
				}
			} else {
				completeSplit = false;
			}
		} else if (doSplit) {
			if (hcint & kHCINT_ACK) {
				ssplitFrameNum = regRead(kDWC2_HFNUM) & kHFNUM_MAX_FRNUM;
				ret = kIOReturnSuccess;
				completeSplit = true;
			}
		}

		if (ret != kIOReturnSuccess) {
			break;
		}

		// A packet shorter than the maximum ends the transfer: that is
		// how USB signals "that is all I have".
		if (!completeSplit && actual < xferLen) {
			stopTransfer = true;
		}

		if (!completeSplit) {
			done += actual;
			// Flip the sequence bit for the next packet, unless this was
			// a SETUP (which always starts DATA0 on the next stage and
			// is driven explicitly by controlTransfer).
			*pid = channelNextPid(kDWC2Channel);
		}
	} while (((done < len) && !stopTransfer) || completeSplit);

	regWrite(hcOffset(kDWC2Channel, kDWC2_HCINTMSK), 0);
	regWrite(hcOffset(kDWC2Channel, kDWC2_HCINT), 0xFFFFFFFF);

	*actualLen = done;
	return ret;
}

IOReturn
BCM2837DWC2::doTransferRetrying(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
                                void *buffer, uint32_t len, uint32_t *actualLen,
                                uint32_t timeoutMS)
{
	/*
	 * NAK means "not now", not "failed" - a device with nothing to send,
	 * or not yet ready to receive, NAKs and expects to be asked again.
	 * Retrying is therefore part of correct operation, but it must be
	 * bounded or a dead device turns into a hung boot.
	 *
	 * Crucially the retry RESUMES rather than restarting. doTransfer can
	 * NAK part-way through a multi-chunk transfer, having already moved
	 * some bytes and advanced the data-toggle bit; re-issuing the whole
	 * transfer from offset zero would re-send data the device already took
	 * (or re-request data it already gave) with a now-wrong toggle, which
	 * USB responds to by dropping the packet as a duplicate. u-boot's
	 * _submit_control_msg does the same advance-then-retry
	 * (`buffer += dev->act_len; len -= dev->act_len;`); an earlier version
	 * of this function restarted from zero, which only escapes notice
	 * while every transfer happens to fit in a single chunk and never
	 * NAKs.
	 */
	uint32_t total = 0;
	uint32_t waited = 0;

	*actualLen = 0;

	for (;;) {
		uint32_t moved = 0;
		IOReturn ret = doTransfer(ep, pid, isIn,
		    (uint8_t *)buffer + total, len - total, &moved);
		total += moved;
		*actualLen = total;

		if (ret != kIOReturnNotReady) {
			return ret;
		}
		// Guarded on len > 0 so a zero-length status-stage packet, whose
		// "complete" length is also zero, is not mistaken for done the
		// moment it NAKs.
		if (len > 0 && total >= len) {
			return kIOReturnSuccess;
		}
		if (waited >= timeoutMS) {
			return kIOReturnTimeout;
		}
		IOSleep(1);
		waited++;
	}
}

IOReturn
BCM2837DWC2::transfer(DWC2Endpoint *ep, void *buffer, uint32_t len, uint32_t *actualLen, uint32_t timeoutMS)
{
	// See fTransferLock's header comment: this and controlTransfer() are
	// the only two ways into the shared channel/DMA buffer, and callers
	// (SMSC9514Ethernet's TX via outputPacket() and RX via fRxTimer) run
	// on genuinely different, unsynchronized threads.
	IOLockLock(fTransferLock);
	IOReturn ret = doTransferRetrying(ep, &ep->toggle, ep->epIsIn, buffer, len, actualLen, timeoutMS);
	IOLockUnlock(fTransferLock);
	return ret;
}

IOReturn
BCM2837DWC2::controlTransfer(DWC2Endpoint *ep, const USBSetupPacket *setup,
                             void *data, uint32_t *actualLen)
{
	if (setup == NULL) {
		return kIOReturnBadArgument;
	}

	uint32_t dummy = 0;
	uint32_t dataLen = setup->wLength;
	bool dataIn = (setup->bmRequestType & kUSBReqDirIn) != 0;
	IOReturn ret;

	if (actualLen != NULL) {
		*actualLen = 0;
	}

	// Locked for the whole SETUP+DATA+STATUS sequence, not per-stage: a
	// foreign transfer (e.g. an RX poll) interleaving between this
	// transfer's own stages would be just as real a corruption as one
	// interleaving mid-chunk within a single stage. See fTransferLock's
	// header comment.
	IOLockLock(fTransferLock);

	// SETUP stage: always OUT, always the SETUP PID, always 8 bytes.
	USBSetupPacket setupCopy = *setup;
	uint8_t pid = kHCPID_SETUP;
	ret = doTransferRetrying(ep, &pid, false, &setupCopy, sizeof(setupCopy), &dummy, 1000);
	if (ret != kIOReturnSuccess) {
		IOLockUnlock(fTransferLock);
		return ret;
	}

	// DATA stage, if any. Control data always starts at DATA1.
	uint32_t moved = 0;
	if (dataLen > 0 && data != NULL) {
		pid = kHCPID_DATA1;
		ret = doTransferRetrying(ep, &pid, dataIn, data, dataLen, &moved, 1000);
		if (ret != kIOReturnSuccess) {
			IOLockUnlock(fTransferLock);
			return ret;
		}
		if (actualLen != NULL) {
			*actualLen = moved;
		}
	}

	/*
	 * STATUS stage: a zero-length packet in the opposite direction from
	 * the data stage. With no data stage at all it is always IN.
	 */
	bool statusIn = (dataLen > 0 && data != NULL) ? !dataIn : true;
	pid = kHCPID_DATA1;
	ret = doTransferRetrying(ep, &pid, statusIn, fDMAVirt, 0, &dummy, 1000);
	IOLockUnlock(fTransferLock);
	return ret;
}

IOReturn
BCM2837DWC2::controlRequest(DWC2Endpoint *ep, uint8_t bmRequestType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                            void *data, uint32_t *actualLen)
{
	USBSetupPacket setup;
	setup.bmRequestType = bmRequestType;
	setup.bRequest = bRequest;
	setup.wValue = wValue;
	setup.wIndex = wIndex;
	setup.wLength = wLength;
	return controlTransfer(ep, &setup, data, actualLen);
}

#pragma mark - UIM primitives

/*
 * See the header for why this exists as a separate entry point rather than
 * the UIM just calling transfer(). Same lock, same engine, caller-owned PID
 * and NAK budget.
 */
IOReturn
BCM2837DWC2::transferStage(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
                           void *buffer, uint32_t len, uint32_t *actualLen,
                           uint32_t timeoutMS)
{
	if (ep == NULL || pid == NULL || actualLen == NULL) {
		return kIOReturnBadArgument;
	}
	// A zero-length transfer (a control status stage) still needs a real
	// buffer pointer to hand the DMA engine, and doTransfer() will not read
	// or write through it at length 0.
	if (buffer == NULL) {
		buffer = fDMAVirt;
	}

	IOLockLock(fTransferLock);
	IOReturn ret = doTransferRetrying(ep, pid, isIn, buffer, len, actualLen, timeoutMS);
	IOLockUnlock(fTransferLock);
	return ret;
}

#pragma mark - Parked asynchronous IN transfers (DAR-370)

/*
 * One kDWC2AsyncBufferSize slice per async channel. Failure is not fatal:
 * with no buffer, fAsyncChannelCount stays 0 and every IN transfer uses
 * channel 0 as before.
 */
bool
BCM2837DWC2::allocateAsyncDMABuffer(void)
{
	void *virt = NULL;
	if (!allocateContiguousDMA(kDWC2AsyncChannels * kDWC2AsyncBufferSize,
	    "async DMA buffer", &fAsyncDMABuffer, &virt, &fAsyncDMABusAddr)) {
		return false;
	}
	fAsyncDMAVirt = (uint8_t *)virt;
	return true;
}

void
BCM2837DWC2::setAsyncNotify(IOInterruptEventSource *source)
{
	fAsyncNotify = source;
	__asm__ volatile ("dmb ish" ::: "memory");
}

bool
BCM2837DWC2::asyncEligible(const DWC2Endpoint *ep, bool isIn) const
{
	if (ep == NULL || !isIn || fAsyncChannelCount == 0 || fChannelWedged) {
		return false;
	}
	// Non-periodic only: a periodic channel halts on every NAK (see
	// waitForChannelSettled()), so parking one would interrupt every
	// (micro)frame. Control endpoints are driven stage by stage.
	if (ep->epType != kHCCHAR_EPTYPE_BULK) {
		return false;
	}
	// The same split decision doTransfer() makes. A split transfer needs
	// its SSPLIT/CSPLIT state machine, which only runs there.
	if (ep->speed != kUSBSpeedHigh && ep->splitHubAddr != 0 &&
	    fRootSpeed == kUSBSpeedHigh) {
		return false;
	}
	return ep->maxPacket != 0;
}

int
BCM2837DWC2::asyncStart(DWC2Endpoint *ep, uint32_t len, uint32_t *xferLenOut)
{
	if (fRegBase == NULL || fAsyncDMAVirt == NULL || !asyncEligible(ep, true)) {
		return -1;
	}

	uint32_t chan = 0;
	for (uint32_t c = 1; c <= fAsyncChannelCount; c++) {
		if (!fAsyncBusy[c] && !fAsyncWedged[c]) {
			chan = c;
			break;
		}
	}
	if (chan == 0) {
		return -1;
	}

	// Chunk sizing as doTransfer() does it: whole packets, bounded by the
	// silicon's counters and by this channel's buffer slice.
	uint32_t maxXferLen = kDWC2MaxPacketCount * ep->maxPacket;
	if (maxXferLen > kDWC2MaxTransferSize) {
		maxXferLen = kDWC2MaxTransferSize;
	}
	if (maxXferLen > kDWC2AsyncBufferSize) {
		maxXferLen = kDWC2AsyncBufferSize;
	}
	maxXferLen = (maxXferLen / ep->maxPacket) * ep->maxPacket;
	uint32_t xferLen = (len > maxXferLen) ? maxXferLen : len;
	uint32_t numPackets = (xferLen > ep->maxPacket) ?
	    (xferLen + ep->maxPacket - 1) / ep->maxPacket : 1;

	channelInit(chan, ep, true, false);

	regWrite(hcOffset(chan, kDWC2_HCTSIZ),
	    (xferLen & kHCTSIZ_XFERSIZE_MASK) |
	    (numPackets << kHCTSIZ_PKTCNT_SHIFT) |
	    ((uint32_t)ep->toggle << kHCTSIZ_PID_SHIFT));
	regWrite(hcOffset(chan, kDWC2_HCDMA),
	    (uint32_t)(fAsyncDMABusAddr + (uint64_t)(chan - 1) * kDWC2AsyncBufferSize));
	regWrite(hcOffset(chan, kDWC2_HCINT), kHCINT_ALL);

	IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
	fPendingHcInt[chan] = 0;
	IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

	fAsyncLen[chan] = xferLen;
	*xferLenOut = xferLen;
	fAsyncBusy[chan] = true;
	fAsyncStarts++;

	// CHHLTD only: a NAK must not interrupt (see the header), and in
	// buffer-DMA mode every other outcome - XFERCOMP, STALL, the errors -
	// halts the channel and so arrives as CHHLTD with its cause alongside.
	regWrite(hcOffset(chan, kDWC2_HCINTMSK), kHCINT_CHHLTD);
	regModify(kDWC2_HAINTMSK, 0, 1U << chan);

	// Same ordering requirement as doTransfer(): everything above must be
	// visible to the core, and fAsyncBusy to interruptOccurred(), before
	// the channel is enabled.
	__asm__ volatile ("dsb sy" ::: "memory");

	regModify(hcOffset(chan, kDWC2_HCCHAR),
	    kHCCHAR_MULTICNT_MASK | kHCCHAR_CHEN | kHCCHAR_CHDIS | kHCCHAR_ODDFRM,
	    (1U << kHCCHAR_MULTICNT_SHIFT) | kHCCHAR_CHEN);

	return (int)chan;
}

void
BCM2837DWC2::asyncRelease(uint32_t chan)
{
	regWrite(hcOffset(chan, kDWC2_HCINTMSK), 0);
	regModify(kDWC2_HAINTMSK, 1U << chan, 0);
	regWrite(hcOffset(chan, kDWC2_HCINT), kHCINT_ALL);

	IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
	fPendingHcInt[chan] = 0;
	IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

	__asm__ volatile ("dmb ish" ::: "memory");
	fAsyncBusy[chan] = false;
}

/*
 * The channel has halted. Decode it, copy out what arrived, carry the data
 * toggle forward and free the channel.
 */
IOReturn
BCM2837DWC2::asyncFinish(uint32_t chan, DWC2Endpoint *ep, uint32_t hcint,
    void *dst, uint32_t dstLen, uint32_t *actualLen)
{
	uint32_t xferLen = fAsyncLen[chan];
	uint32_t actual = 0;
	IOReturn ret;

	// While parked, every NAK the device answered with still latched
	// HCINT.NAK (HCINTMSK gates only the interrupt; QEMU's model ORs it in
	// on each retry, hcd-dwc2.c:359-368). Those NAKs are history, not the
	// outcome, and decodeChannelResult() tests NAK before STALL and the
	// errors, so they have to go before it sees the bits.
	hcint &= ~(kHCINT_NAK | kHCINT_NYET);

	const uint32_t kOutcomeBits = kHCINT_XFERCOMP | kHCINT_STALL | kHCINT_XACTERR |
	    kHCINT_BBLERR | kHCINT_AHBERR | kHCINT_DATATGLERR | kHCINT_FRMOVRUN;
	if (hcint & kOutcomeBits) {
		ret = decodeChannelResult(chan, hcint, xferLen, true, &actual);
	} else {
		// A halt with no outcome of its own: asyncAbort() took the
		// channel down. That is not a failure worth decoding.
		ret = kIOReturnAborted;
	}

	if (ret != kIOReturnSuccess) {
		// Packets that arrived before an abort or error are real and
		// already acknowledged, so count them from HCTSIZ the same way
		// decodeChannelResult() does for a completed IN.
		actual = channelInBytesDone(chan, xferLen);
	}

	if (actual > dstLen) {
		IOLog("BCM2837DWC2: async channel %u delivered %u bytes into a %u byte "
		      "buffer - truncated\n", chan, actual, dstLen);
		actual = dstLen;
		if (ret == kIOReturnSuccess) {
			ret = kIOReturnOverrun;
		}
	}
	if (actual > 0) {
		__asm__ volatile ("dsb sy" ::: "memory");
		memcpy(dst, fAsyncDMAVirt + (chan - 1) * kDWC2AsyncBufferSize, actual);
	}

	// HCTSIZ.PID is the toggle the core expects next, whatever happened:
	// unchanged if nothing arrived, advanced past every packet that did.
	// doTransfer() takes it the same way.
	ep->toggle = channelNextPid(chan);

	asyncRelease(chan);

	*actualLen = actual;
	return ret;
}

bool
BCM2837DWC2::asyncCollect(int chanIn, DWC2Endpoint *ep, void *dst, uint32_t dstLen,
    uint32_t *actualLen, IOReturn *status, uint32_t *hcintOut)
{
	*actualLen = 0;
	*hcintOut = 0;
	if (chanIn <= 0 || (uint32_t)chanIn > fAsyncChannelCount || !fAsyncBusy[chanIn]) {
		*status = kIOReturnBadArgument;
		return true;
	}
	uint32_t chan = (uint32_t)chanIn;

	// Take what the interrupt handler recorded, plus anything still in the
	// register. The register read is the same poll fallback
	// waitForChannelInt() keeps for channel 0: a missed interrupt must
	// delay a completion, not lose it.
	uint32_t raw = regRead(hcOffset(chan, kDWC2_HCINT));
	if (raw != 0) {
		regWrite(hcOffset(chan, kDWC2_HCINT), raw);
	}
	IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
	fPendingHcInt[chan] |= raw;
	uint32_t hcint = fPendingHcInt[chan];
	if (hcint & kHCINT_CHHLTD) {
		fPendingHcInt[chan] = 0;
	}
	IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

	if (!(hcint & kHCINT_CHHLTD)) {
		return false;
	}

	*hcintOut = hcint & ~(kHCINT_NAK | kHCINT_NYET);
	*status = asyncFinish(chan, ep, hcint, dst, dstLen, actualLen);
	return true;
}

IOReturn
BCM2837DWC2::asyncAbort(int chanIn, DWC2Endpoint *ep, void *dst, uint32_t dstLen,
    uint32_t *actualLen)
{
	*actualLen = 0;
	if (chanIn <= 0 || (uint32_t)chanIn > fAsyncChannelCount || !fAsyncBusy[chanIn]) {
		return kIOReturnBadArgument;
	}
	uint32_t chan = (uint32_t)chanIn;

	uint32_t hcint = 0;
	haltChannel(chan, &hcint);

	IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
	hcint |= fPendingHcInt[chan];
	fPendingHcInt[chan] = 0;
	IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

	if (fAsyncWedged[chan]) {
		// haltChannel() gave up on it. Free the bookkeeping, not the
		// channel: fAsyncWedged keeps asyncStart() away from it for good.
		ep->toggle = channelNextPid(chan);
		asyncRelease(chan);
		return kIOReturnNotResponding;
	}

	IOReturn ret = asyncFinish(chan, ep, hcint, dst, dstLen, actualLen);
	return (ret == kIOReturnSuccess) ? kIOReturnSuccess : kIOReturnAborted;
}

uint32_t
BCM2837DWC2::frameNumber(void) const
{
	if (fRegBase == NULL) {
		return 0;
	}
	return regRead(kDWC2_HFNUM) & kHFNUM_MAX_FRNUM;
}

uint32_t
BCM2837DWC2::rootPortRegister(void) const
{
	if (fRegBase == NULL) {
		return 0;
	}
	return regRead(kDWC2_HPRT0);
}

IOReturn
BCM2837DWC2::rootPortFeature(uint16_t feature, bool set)
{
	if (fRegBase == NULL) {
		return kIOReturnNotReady;
	}

	switch (feature) {
	case kHubFeatPortPower:
		if (set) {
			return portPowerOn() ? kIOReturnSuccess : kIOReturnIOError;
		}
		portWriteHPRT0(kHPRT0_PRTPWR, 0);
		return kIOReturnSuccess;

	case kHubFeatPortReset:
		// Only meaningful as a SET; the hub protocol has the hub clear
		// PRTRST itself, which portReset() already does as part of its
		// sequence.
		if (!set) {
			return kIOReturnSuccess;
		}
		return portReset() ? kIOReturnSuccess : kIOReturnIOError;

	case kHubFeatPortEnable:
		/*
		 * HPRT0.PRTENA is write-1-to-CLEAR, which is exactly backwards from
		 * how it reads, and is why portWriteHPRT0() masks it out of every
		 * read-modify-write. A hub can only ever disable a port, never
		 * enable one directly (USB 2.0 11.24.2.7.2) - the port becomes
		 * enabled as a side effect of a reset - so SET is a no-op and CLEAR
		 * writes the bit.
		 */
		if (set) {
			return kIOReturnUnsupported;
		}
		regWrite(kDWC2_HPRT0,
		    (regRead(kDWC2_HPRT0) & ~kHPRT0_W1C_MASK) | kHPRT0_PRTENA);
		return kIOReturnSuccess;

	// The change bits are write-1-to-clear status, so only CLEAR_FEATURE
	// does anything; SET_FEATURE on them is not a legal hub request.
	case kHubFeatCPortConnection:
		if (set) {
			return kIOReturnUnsupported;
		}
		regWrite(kDWC2_HPRT0,
		    (regRead(kDWC2_HPRT0) & ~kHPRT0_W1C_MASK) | kHPRT0_PRTCONNDET);
		return kIOReturnSuccess;

	case kHubFeatCPortEnable:
		if (set) {
			return kIOReturnUnsupported;
		}
		regWrite(kDWC2_HPRT0,
		    (regRead(kDWC2_HPRT0) & ~kHPRT0_W1C_MASK) | kHPRT0_PRTENCHNG);
		return kIOReturnSuccess;

	case kHubFeatCPortOverCurrent:
		if (set) {
			return kIOReturnUnsupported;
		}
		regWrite(kDWC2_HPRT0,
		    (regRead(kDWC2_HPRT0) & ~kHPRT0_W1C_MASK) | kHPRT0_PRTOVRCURRCHNG);
		return kIOReturnSuccess;

	case kHubFeatCPortReset:
		// DWC2 has no reset-change bit; portReset() is synchronous, so the
		// change is already consumed by the time anyone could ask.
		return kIOReturnSuccess;

	default:
		// Suspend and the indicator features are genuinely not implemented
		// on this controller - it never suspends a port. Saying so beats
		// silently reporting success for something that did not happen.
		IOLog("BCM2837DWC2: root port feature %u (%s) not implemented\n",
		    feature, set ? "set" : "clear");
		return kIOReturnUnsupported;
	}
}

#pragma mark - IOService

bool
BCM2837DWC2::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	IOLog("BCM2837DWC2: start\n");

	fTransferLock = IOLockAlloc();
	if (fTransferLock == NULL) {
		IOLog("BCM2837DWC2: IOLockAlloc failed\n");
		return false;
	}

	fInterruptLock = IOLockAlloc();
	if (fInterruptLock == NULL) {
		IOLog("BCM2837DWC2: IOLockAlloc (interrupt) failed\n");
		return false;
	}
	fStatusLock = IOSimpleLockAlloc();
	if (fStatusLock == NULL) {
		IOLog("BCM2837DWC2: IOSimpleLockAlloc failed\n");
		return false;
	}

	fEnumLock = IOLockAlloc();
	if (fEnumLock == NULL) {
		IOLog("BCM2837DWC2: IOLockAlloc (enum) failed\n");
		return false;
	}
	fRecoveryCall = thread_call_allocate(recoveryFired, this);
	fBSDRegisterLock = IOLockAlloc();
	if (fBSDRegisterLock == NULL) {
		IOLog("BCM2837DWC2: IOLockAlloc (BSD register) failed\n");
		return false;
	}

	IODeviceMemory *devMem = provider->getDeviceMemoryWithIndex(0);
	if (devMem == NULL) {
		IOLog("BCM2837DWC2: no device memory on provider\n");
		return false;
	}

	fRegMap = devMem->map();
	if (fRegMap == NULL) {
		IOLog("BCM2837DWC2: register map() failed\n");
		return false;
	}
	fRegBase = (volatile uint8_t *)fRegMap->getVirtualAddress();

	/*
	 * GSNPSID is the core's own identity register: 'OT' in the top half
	 * plus a BCD release. Reading it before touching anything else is the
	 * cheapest possible proof that we are talking to real DWC2 registers
	 * and not, say, unmapped memory reading back as zero - which is
	 * exactly what this address did before the QEMU model was backported,
	 * and is a failure mode worth naming rather than debugging twice.
	 */
	uint32_t snpsid = regRead(kDWC2_GSNPSID);
	if ((snpsid & 0xFFFFF000) != 0x4F542000) {
		IOLog("BCM2837DWC2: GSNPSID=%#x is not a DWC2 core "
		      "(expected 0x4F54xxxx) - wrong address or no controller\n", snpsid);
		return false;
	}
	IOLog("BCM2837DWC2: GSNPSID=%#x GHWCFG2=%#x GHWCFG3=%#x\n",
	    snpsid, regRead(kDWC2_GHWCFG2), regRead(kDWC2_GHWCFG3));

	if (!allocateDMABuffer()) {
		return false;
	}

	if (!coreInit()) {
		IOLog("BCM2837DWC2: core init failed\n");
		return false;
	}
	IOLog("BCM2837DWC2: core init done, %u host channels\n", fNumChannels);

	// DAR-370: lend channels 1..N to parked async IN transfers.
	// dwc2-no-async=1 (or a bare dwc2-no-async) keeps every transfer on
	// channel 0, as before DAR-370.
	fAsyncChannelCount = 0;
	int noAsync = 0;
	bool asyncOff = PE_parse_boot_argn("dwc2-no-async", &noAsync, sizeof(noAsync)) &&
	    noAsync != 0;
	uint32_t spare = (fNumChannels > 1) ? fNumChannels - 1 : 0;
	if (!asyncOff && spare > 0 && allocateAsyncDMABuffer()) {
		fAsyncChannelCount = (spare < kDWC2AsyncChannels) ? spare : kDWC2AsyncChannels;
	}
	IOLog("BCM2837DWC2: %u channel(s) for parked async IN transfers\n",
	    fAsyncChannelCount);

	if (!hostInit()) {
		IOLog("BCM2837DWC2: host init failed\n");
		return false;
	}

	/*
	 * Register the real interrupt handler before opening the master gate
	 * (GAHBCFG.GLBLINTRMSK below) - deliberately in that order, so there is
	 * no window where hardware could raise the line before anything is
	 * listening. GINTMSK/HAINTMSK for channel 0 are already unmasked (see
	 * coreInit()); this is the only remaining, and last, level of the
	 * hierarchy.
	 */
	// DAR-316's real-hardware DHCP/write-path bugs were bisected against
	// this too (a `dwc2-poll=1` boot-arg gate, skipping interrupt
	// registration entirely to run the pre-DAR-136 pure-polled path,
	// removed once interrupt-driven completion was conclusively
	// exonerated as a cause and the real root causes were found and
	// fixed instead - see BCM2837SDHCI.h's class header and the DAR-316
	// Linear history for the full trail). waitForChannelSettled()'s own
	// register-poll fallback remains as a safety net regardless.
	IOReturn intResult = provider->registerInterrupt(0, this, &BCM2837DWC2::interruptOccurred);
	if (intResult != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: registerInterrupt failed, %#x\n", intResult);
		return false;
	}
	intResult = provider->enableInterrupt(0);
	if (intResult != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: enableInterrupt failed, %#x\n", intResult);
		return false;
	}
	regModify(kDWC2_GAHBCFG, 0, kGAHBCFG_GLBLINTRMSK);

	if (!portPowerOn()) {
		IOLog("BCM2837DWC2: port power-on failed\n");
		return false;
	}

	/*
	 * usb-apple-hub=1 (DAR-353): AppleUSBHub, matched on the family's root
	 * hub device, owns the bus - it resets the root port and enumerates
	 * everything downstream itself, through the UIM, with hot-plug. This
	 * driver then only brings the core up and starts the controller; its
	 * own one-shot enumeration below would be a second, competing
	 * enumerator for the same ports. The port stays powered so the root
	 * hub's first status read reports the connection.
	 */
	uint32_t appleHub = 0;
	fAppleHub = PE_parse_boot_argn("usb-apple-hub", &appleHub, sizeof(appleHub)) &&
	    appleHub != 0;
	if (fAppleHub) {
		IOLog("BCM2837DWC2: usb-apple-hub: AppleUSBHub enumerates the bus "
		      "(DAR-353); skipping boot-time enumeration\n");
	} else if (!portWaitForConnect(1000)) {
		/*
		 * Nothing plugged in. On a real Pi 3 the LAN9514 is soldered on
		 * and this should not happen; under QEMU it just means no
		 * -device was attached to the USB bus. Either way the controller
		 * itself is up, so publish it rather than failing to start - a
		 * host controller with an empty port is a working host
		 * controller.
		 */
		IOLog("BCM2837DWC2: no device on root port (HPRT0=%#x)\n",
		    regRead(kDWC2_HPRT0));
		registerService();
		return true;
	}

	if (!fAppleHub) {
		if (!portReset()) {
			IOLog("BCM2837DWC2: root port reset failed\n");
			registerService();
			return true;
		}

		static const char *speedNames[] = { "high", "full", "low", "reserved" };
		IOLog("BCM2837DWC2: root port enabled, %s-speed device attached (HPRT0=%#x)\n",
		    speedNames[fRootSpeed & 3], regRead(kDWC2_HPRT0));

		// Walk the bus. A failure here leaves a working but empty controller
		// rather than failing to start - the difference matters for
		// diagnosis, since "controller up, enumeration failed" and
		// "controller dead" have completely different causes.
		enumerateBus();
	}

	/*
	 * Resolve (and log) the board's real Ethernet identity from the
	 * VideoCore mailbox, ahead of the Ethernet block below.
	 *
	 * SMSC9514Ethernet::start() is the actual consumer - the LAN9514 on a
	 * Pi has no EEPROM, so the firmware's answer is the only real MAC
	 * address that exists for this board (DAR-144). But that start() is
	 * unreachable in QEMU for the reason the next comment spells out
	 * (findEthernetFunction() never matches a generic usb-net), so
	 * priming here is what gives the mailbox path a real, exercised,
	 * boot-log-visible run in emulation instead of leaving it
	 * real-hardware-only code. The result is cached, so that later call
	 * costs nothing and cannot produce a second, racing mailbox
	 * transaction. Failure is not fatal: SMSC9514Ethernet has its own
	 * last-resort fallback.
	 */
	BCM2837Mailbox::primeBoardIdentity();

	/*
	 * The one IONetworkStack instance this kernel ever creates.
	 *
	 * This used to live inside the SMSC block below, which made it
	 * real-hardware-only: under QEMU no SMSC part enumerates, so no stack was
	 * ever created, so any IOEthernetInterface published by any other driver
	 * could never reach BSD. That is no longer a hypothetical - the real
	 * AppleUSBCDCECMData driver publishes exactly such an interface for
	 * QEMU's usb-net once the IOUSBController below starts publishing nubs
	 * (DAR-243). Nothing in this hand-built personality tree
	 * (BCM2837Personalities.cpp) ever matches or starts an IONetworkStack,
	 * and IOService::serviceMatching("IONetworkStack") only finds an
	 * ALREADY-registered instance rather than creating one, so if this does
	 * not run, IORegisterNetworkInterface() blocks forever in
	 * waitForService().
	 *
	 * It must exist before any interface is published, hence here rather than
	 * after the driver blocks below.
	 */
	IONetworkStack *stack = new IONetworkStack;
	if (stack != NULL && stack->init() && stack->attach(this)) {
		if (stack->start(this)) {
			fNetworkStack = stack;
		} else {
			IOLog("BCM2837DWC2: IONetworkStack start() failed - no interface can reach BSD\n");
			stack->detach(this);
			stack->release();
		}
	} else {
		IOLog("BCM2837DWC2: IONetworkStack init/attach failed - no interface can reach BSD\n");
		if (stack != NULL) {
			stack->release();
		}
	}

	/*
	 * On real macOS, naming an interface and attaching it to BSD is driven
	 * from userspace by configd's InterfaceNamer opening a user client on
	 * IONetworkStack. This port has no configd, so the kernel-side path has
	 * to be triggered here instead.
	 *
	 * A publish notification rather than a direct call per driver, because
	 * the interface this now has to catch is not created by anything on this
	 * thread: AppleUSBCDCECMData publishes its IOEthernetInterface from
	 * IOKit's own matching thread, some time after start() has returned. A
	 * notification catches both that and SMSC9514Ethernet's, through one
	 * code path, which is also why the SMSC block below no longer registers
	 * its interface by hand.
	 *
	 * gIOPublishNotification at priority 0, and BOTH halves of that are
	 * load-bearing rather than defaults.
	 *
	 * IONetworkStack registers its own handler for this same event on
	 * gIOPublishNotification at priority 1000 (IONetworkStack.cpp:176-184),
	 * and that handler is what moves an interface into
	 * kInterfaceStatePublished and onto the stack's naming list.
	 * IORegisterNetworkInterface() then refuses to name anything that is not
	 * already in that state ("unable to name interface in state 0x0 as en0",
	 * IONetworkStack.cpp:690). So this handler MUST run after the stack's.
	 *
	 * IOKit orders notification handlers of the SAME type by priority,
	 * highest first (IONotifyOrdering, IOService.cpp:3472-3495), so priority
	 * 0 here is deterministically after the stack's 1000. The first version
	 * of this used gIOFirstPublishNotification, which is a different
	 * notification type and therefore a different ordered list - no defined
	 * ordering against the stack's handler at all. It lost the race on a real
	 * boot, every time, with exactly the state-0x0 message above.
	 */
	fInterfaceNotifier = addMatchingNotification(gIOPublishNotification,
	    serviceMatching("IONetworkInterface"), &BCM2837DWC2::interfacePublished,
	    this, NULL, 0);
	if (fInterfaceNotifier == NULL) {
		IOLog("BCM2837DWC2: could not install IONetworkInterface notification - "
		      "no interface will reach BSD\n");
	}

	/*
	 * BRING-UP SCAFFOLDING, not the final wiring.
	 *
	 * This is an SMSC-specific call inside an otherwise chip-agnostic host
	 * controller, which is the wrong home for it long-term - the real fix
	 * is a personality-based match once this driver publishes a nub per
	 * enumerated USB device (it does not yet; BCM2837USBEnum.cpp's device
	 * table is internal to this class, not IOService objects other drivers
	 * could match against). It is here because nothing else in this tree
	 * can reach the LAN9514's Ethernet function at all otherwise: the SMSC
	 * vendor-request register protocol was unverified against real silicon
	 * until this session (QEMU models no LAN9514), and the device only
	 * became reachable in the first place with the HFIR fix in
	 * portReset().
	 *
	 * probe() is read-only and cannot become load-bearing - its result is
	 * logged, not consulted, and a board with no SMSC part (every QEMU
	 * run, which substitutes a generic usb-net) simply finds no device and
	 * skips this whole block. What follows it is NOT read-only: once
	 * probe() confirms the register protocol is real, a genuine
	 * SMSC9514Ethernet is constructed, attached, and started by hand -
	 * IONetworkingFamily is landed now (iokit/Network/, this kernel
	 * build), so it can actually create a live IOEthernetInterface rather
	 * than just proving the transport works. attach()+start() are called
	 * directly rather than through registerService()+matching because
	 * there is no nub for a personality to match against (see above) -
	 * this is the same manual-construction shortcut IOService's own
	 * matching engine takes internally, just without a matching dictionary
	 * driving it.
	 */
	const DWC2USBDevice *ethFunction =
	    SMSC9514USBTransport::findEthernetFunction(this);
	if (ethFunction != NULL) {
		SMSC9514USBTransport *transport = new SMSC9514USBTransport;
		if (transport != NULL && transport->init(this, ethFunction) && transport->probe()) {
			SMSC9514Ethernet *eth = new SMSC9514Ethernet;
			if (eth != NULL && eth->init(NULL, transport) && eth->attach(this)) {
				if (eth->start(this)) {
					fEthernetTransport = transport;
					fEthernetDriver = eth;
					transport = NULL; // ownership moved to fEthernetTransport

					/*
					 * eth->start() (attachInterface(), inside
					 * SMSC9514Ethernet::start()) only creates the
					 * IOEthernetInterface inside the IORegistry - it does
					 * NOT make it visible to BSD/ifconfig. That second half
					 * used to be done by an explicit
					 * IORegisterNetworkInterface() call right here; it is now
					 * done by the IONetworkInterface publish notification
					 * installed above, which catches this interface and the
					 * CDC-ECM one through the same code path.
					 *
					 * Registering explicitly as well is done deliberately
					 * rather than relying on notification ordering: the
					 * notification is asynchronous, and this is the real
					 * hardware path that is already verified working. Both
					 * routes funnel through registerInterfaceWithBSD(), which
					 * is idempotent, so whichever runs second does nothing.
					 * eth's fNetif is private for exactly this one caller -
					 * see getEthernetInterface()'s header comment.
					 */
					registerInterfaceWithBSD(eth->getEthernetInterface());
				} else {
					IOLog("BCM2837DWC2: SMSC9514Ethernet start() failed\n");
					eth->detach(this);
					eth->release();
				}
			} else {
				IOLog("BCM2837DWC2: SMSC9514Ethernet init/attach failed\n");
				if (eth != NULL) {
					eth->release();
				}
			}
		}
		// Either genuinely no probe(), or ownership was handed off above
		// (transport == NULL in that case) - safe either way.
		delete transport;
	}

	startUSBHostController();

	/*
	 * dwc2-test-wedge=<seconds> (DAR-352 verification only): after that
	 * delay, mark channel 0 wedged exactly as haltChannel() does on a
	 * forced halt that never completes, which runs the real recovery path.
	 * The wedge itself came from real-hardware timing and cannot be made to
	 * happen in QEMU.
	 */
	uint32_t testWedge = 0;
	if (PE_parse_boot_argn("dwc2-test-wedge", &testWedge, sizeof(testWedge)) &&
	    testWedge != 0) {
		fTestWedgeCall = thread_call_allocate(testWedgeFired, this);
		if (fTestWedgeCall != NULL) {
			uint64_t deadline;
			clock_interval_to_deadline(testWedge, kSecondScale, &deadline);
			thread_call_enter_delayed(fTestWedgeCall, deadline);
			IOLog("BCM2837DWC2: dwc2-test-wedge in %u s\n", testWedge);
		}
	}

	registerService();
	return true;
}

/*
 * Stand up the real IOUSBFamily controller over this driver and publish an
 * IOUSBDevice nub for every device enumerateBus() already found.
 *
 * Ordering matters in two ways that are easy to get wrong:
 *
 *  - This runs AFTER enumerateBus(), because the UIM reserves the USB
 *    addresses that enumeration already assigned (so the root hub simulation
 *    cannot be handed an address that aliases a real device) and builds its
 *    endpoint state from that device table.
 *
 *  - It runs on the IOKit configuration thread, not the USB workloop.
 *    IOUSBDevice::start() - reached from PublishEnumeratedDevice() via
 *    CreateDevice() - issues SYNCHRONOUS device requests, and IOUSBFamily
 *    explicitly refuses those from the workloop thread
 *    (IOUSBController_Pipes.cpp:363-367, kIOUSBSyncRequestOnWLThread). The
 *    queue engine servicing those requests runs on the workloop, so this
 *    thread genuinely has to be a different one - which it is.
 */
void
BCM2837DWC2::startUSBHostController(void)
{
	// With usb-apple-hub (DAR-353) nothing is enumerated here by design:
	// the controller's root hub is what AppleUSBHub enumerates from.
	if (fNumDevices == 0 && !fAppleHub) {
		// Nothing enumerated - a controller with no devices to publish would
		// still start cleanly, but there is nothing for it to do and saying
		// so beats a silent no-op.
		IOLog("BCM2837DWC2: no enumerated devices, not starting IOUSBController\n");
		return;
	}

	/*
	 * Give ourselves a registry location before starting the controller.
	 *
	 * This is not cosmetic - without it the boot panics. Every USB controller
	 * IOUSBFamily was written for hangs off an IOPCIDevice, which always has a
	 * location string ("1f,2" and the like), and
	 * IOUSBController::CreateRootHubDevice() relies on that unconditionally:
	 *
	 *     (*rootHubDevice)->setLocation(provider->getLocation());
	 *     (*rootHubDevice)->setLocation(provider->getLocation(), gIOUSBPlane);
	 *
	 * (IOUSBController.cpp:3743-3744). Neither call is NULL-guarded, unlike
	 * the ParsePCILocation() call twenty lines above them, so a provider with
	 * no location - which is what this driver is, being matched by name off
	 * an IOPlatformDevice rather than by PCI address - reaches
	 * OSSymbol::withCString(NULL) and takes a kernel data abort inside
	 * OSSymbolPool::findSymbol(). Confirmed by resolving the real panic PC
	 * against the unstripped kernel, not deduced from reading alone.
	 *
	 * "0" rather than a fake PCI-looking string: ParsePCILocation() parses it
	 * to device 0, function 0, which yields USB bus number 0 - correct, since
	 * this board has exactly one USB controller.
	 */
	setLocation("0");

	BCM2837USBHostController *controller = new BCM2837USBHostController;
	if (controller == NULL) {
		IOLog("BCM2837DWC2: IOUSBController allocation failed\n");
		return;
	}

	if (!controller->init(NULL) || !controller->attach(this)) {
		IOLog("BCM2837DWC2: IOUSBController init/attach failed\n");
		controller->release();
		return;
	}

	if (!controller->start(this)) {
		IOLog("BCM2837DWC2: IOUSBController start() failed - no USB nubs will "
		      "be published\n");
		controller->detach(this);
		controller->release();
		return;
	}

	fUSBController = controller;

	/*
	 * Publish a nub per enumerated device.
	 *
	 * Hubs are skipped on purpose. A published IOUSBDevice for a hub would be
	 * matched by AppleUSBHub - if this kernel had it, which it does not - and
	 * that driver's whole job is to enumerate downstream ports, which
	 * BCM2837DWC2::enumerateBus() has already done. Publishing one would
	 * either do nothing or set up a second, competing enumerator for the same
	 * ports on a single-channel controller.
	 */
	uint32_t published = publishEnumeratedDevices();

	IOLog("BCM2837DWC2: IOUSBController up, %u device nub(s) published\n", published);
}

// Publish every enumerated device boot's rules allow (publishable()).
// Shared by startUSBHostController() and recoverController().
uint32_t
BCM2837DWC2::publishEnumeratedDevices(void)
{
	uint32_t published = 0;
	for (uint32_t i = 0; i < fNumDevices; i++) {
		const DWC2USBDevice *dev = deviceAt(i);
		if (!publishable(dev)) {
			continue;
		}
		if (fUSBController->PublishEnumeratedDevice(dev) == kIOReturnSuccess) {
			published++;
		}
	}
	return published;
}

#pragma mark - Controller recovery (DAR-352)

/*
 * A forced halt that never completes (haltChannel()) leaves channel 0
 * unusable; fChannelWedged stops the retry storm, but until DAR-352 only a
 * power cycle brought USB back. Recovery is the boot sequence again:
 *
 *   1. remove every published device (their drivers see termination, as
 *      on an unplug) and take back whatever the UIM still has queued or
 *      parked on channels 1-4;
 *   2. with the global interrupt masked and both locks held, core soft
 *      reset and host re-init (coreInit(), hostInit() - exactly start()'s
 *      calls) and clear the wedge flags;
 *   3. root port power, connect and reset (start()'s calls again);
 *   4. enumerateBus() and republish with boot's rules.
 *
 * A core soft reset drops every device's address, which is why 1 and 4 are
 * not optional. Runs on fRecoveryCall, never inline: the wedge is detected
 * in haltChannel() with fTransferLock held, and recovery takes that lock
 * and blocks. At most kMaxRecoveries per boot, so hardware that wedges
 * again straight away does not loop forever.
 *
 * NOT covered: the hand-wired SMSC9514Ethernet on real hardware. It holds
 * its DWC2Endpoints from start() and has no re-attach path, and the LAN9514
 * loses its register setup in the USB reset, so its networking stays down
 * after a recovery until a reboot (DAR-352 item 3). Logged explicitly.
 */
#define kMaxRecoveries 3

void
BCM2837DWC2::scheduleRecovery(void)
{
	if (fRecoveryCall != NULL && !fStopping && !fRecovering &&
	    fRecoveries < kMaxRecoveries) {
		thread_call_enter(fRecoveryCall);
	}
}

void
BCM2837DWC2::recoveryFired(thread_call_param_t owner, thread_call_param_t)
{
	((BCM2837DWC2 *)owner)->recoverController();
}

void
BCM2837DWC2::testWedgeFired(thread_call_param_t owner, thread_call_param_t)
{
	BCM2837DWC2 *me = (BCM2837DWC2 *)owner;
	IOLog("BCM2837DWC2: dwc2-test-wedge: marking channel 0 wedged, as "
	      "haltChannel() does when a forced halt never completes\n");
	IOLockLock(me->fTransferLock);
	me->fChannelWedged = true;
	me->scheduleRecovery();
	IOLockUnlock(me->fTransferLock);
}

/*
 * Steps 2 and 3. Called with fEnumLock held (no re-enumeration may run
 * concurrently); takes fTransferLock itself so no synchronous transfer runs
 * against a resetting core.
 */
bool
BCM2837DWC2::reinitController(void)
{
	IOLockLock(fTransferLock);

	regModify(kDWC2_GAHBCFG, kGAHBCFG_GLBLINTRMSK, 0);

	bool ok = coreInit() && hostInit();
	if (ok) {
		IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
		for (uint32_t c = 0; c < kDWC2MaxChannels; c++) {
			fPendingHcInt[c] = 0;
		}
		IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);
		for (uint32_t c = 0; c < kDWC2MaxChannels; c++) {
			fAsyncBusy[c] = false;
			fAsyncWedged[c] = false;
		}
		regModify(kDWC2_GAHBCFG, 0, kGAHBCFG_GLBLINTRMSK);
	}
	if (!ok) {
		IOLockUnlock(fTransferLock);
		IOLog("BCM2837DWC2: recovery: core/host re-init failed\n");
		return false;
	}

	// Root-port bring-up still under fTransferLock, with channel 0 still
	// marked wedged: nothing (in particular the hand-wired SMSC9514Ethernet
	// on real hardware, which recovery does not unbind) may run a transfer
	// against a bus in reset. reenumerateDevice() holds the lock around
	// portReset() for the same reason.
	// Under usb-apple-hub (DAR-353) the port is AppleUSBHub's: just power
	// it, and let the connection change the core reset produces reach the
	// hub through the root hub's status pipe, so the hub removes the old
	// devices and resets and re-enumerates the port itself.
	bool portOK = fAppleHub ? portPowerOn() :
	    (portPowerOn() && portWaitForConnect(1000) && portReset());
	if (portOK) {
		fChannelWedged = false;
	}
	IOLockUnlock(fTransferLock);

	if (!portOK) {
		IOLog("BCM2837DWC2: recovery: root port did not come back (HPRT0=%#x)\n",
		    regRead(kDWC2_HPRT0));
		return false;
	}
	return true;
}

void
BCM2837DWC2::recoverController(void)
{
	if (fRecovering || fRecoveries >= kMaxRecoveries) {
		return;
	}
	fRecovering = true;
	fRecoveries++;
	OSIncrementAtomic((volatile SInt32 *)&fRecoveryGen);
	IOLog("BCM2837DWC2: recovery %u/%u: channel 0 wedged - resetting the "
	      "controller and re-enumerating the bus (DAR-352)\n",
	    fRecoveries, (uint32_t)kMaxRecoveries);

	// 1. Remove the published devices and drain the UIM.
	// With AppleUSBHub owning the bus (usb-apple-hub, DAR-353) the devices
	// are its children, not ours: the core reset below drops the root
	// port, AppleUSBHub sees the connection change and removes and
	// re-enumerates them itself, the way a Mac recovers a reset port.
	if (fUSBController != NULL) {
		if (fAppleHub) {
			// The core reset below forgets every parked async channel;
			// the UIM's endpoints must forget them too.
			fUSBController->PurgeAllEndpoints();
		} else {
			fUSBController->RemoveAllDevices();
		}
	}
	if (fEthernetDriver != NULL) {
		IOLog("BCM2837DWC2: recovery: SMSC9514Ethernet cannot be re-bound after "
		      "a controller reset (DAR-352 item 3) - its networking stays down "
		      "until a reboot\n");
	}

	// 2-4.
	IOLockLock(fEnumLock);
	bool ok = reinitController();
	uint32_t found = (ok && !fAppleHub) ? enumerateBus() : 0;
	// Bump again now the table is rebuilt: an operation that snapshotted
	// the generation after the first bump (above) but before the rebuild
	// still names the old table, and must not act on the new one.
	OSIncrementAtomic((volatile SInt32 *)&fRecoveryGen);
	IOLockUnlock(fEnumLock);

	uint32_t published = 0;
	if (ok && fUSBController != NULL && !fAppleHub) {
		published = publishEnumeratedDevices();
	}
	if (ok && fUSBController != NULL && fAppleHub) {
		fUSBController->RootPortReconnected();
	}
	if (fAppleHub) {
		IOLog("BCM2837DWC2: recovery %u %s: root port connection change reported; "
		      "AppleUSBHub re-enumerates\n", fRecoveries, ok ? "done" : "FAILED");
	} else {
		IOLog("BCM2837DWC2: recovery %u %s: %u device(s) enumerated, %u republished\n",
		    fRecoveries, ok ? "done" : "FAILED", found, published);
	}

	fRecovering = false;

	// A wedge during this recovery (e.g. one of enumerateBus()'s own control
	// transfers) found fRecovering set and could not schedule another; a
	// failed recovery likewise leaves channel 0 wedged. Try again while
	// attempts remain.
	if (!ok || fChannelWedged) {
		if (fRecoveries < kMaxRecoveries) {
			scheduleRecovery();
		} else {
			IOLog("BCM2837DWC2: recovery attempts exhausted (%u) - USB stays "
			      "down until a reboot\n", (uint32_t)kMaxRecoveries);
		}
	}
}

/*
 * Complete an interface's BSD registration, once. Idempotent by design:
 * IONetworkStack::attachNetworkInterfaceToBSD() sets kIOBSDNameKey and then
 * RE-REGISTERS the interface (IONetworkStack.cpp:824-826), which can re-fire
 * the publish notification for an interface that is already fully attached.
 * Without this guard that second pass would try to name the same interface
 * twice.
 *
 * DAR-297: that guard alone is only safe against a second SEQUENTIAL call --
 * start()'s direct call and interfacePublished()'s notification callback
 * are documented (see the call site in start()) as asynchronous with
 * respect to each other, so two threads can both observe
 * getProperty(gIOBSDNameKey) == NULL before either has called
 * IORegisterNetworkInterface(). fBSDRegisterLock closes that window: the
 * check and the register call now happen as one atomic section.
 */
void
BCM2837DWC2::registerInterfaceWithBSD(IOService *netif)
{
	if (netif == NULL) {
		return;
	}
	IOLockLock(fBSDRegisterLock);
	// gIOBSDNameKey, not the kIOBSDNameKey string macro: that one lives in
	// <IOKit/IOBSD.h>, while the OSSymbol is already declared by IOService.h
	// and is what the property is actually keyed on.
	if (netif->getProperty(gIOBSDNameKey) != NULL) {
		IOLockUnlock(fBSDRegisterLock);
		return; // already named and attached
	}
	bool ok = IORegisterNetworkInterface(netif);
	IOLockUnlock(fBSDRegisterLock);
	if (!ok) {
		IOLog("BCM2837DWC2: IORegisterNetworkInterface failed - interface will "
		      "not appear in ifconfig\n");
	}
}

bool
BCM2837DWC2::interfacePublished(void *target, void *ref, IOService *newService,
                                IONotifier *notifier)
{
#pragma unused (ref, notifier)
	BCM2837DWC2 *me = OSDynamicCast(BCM2837DWC2, (OSMetaClassBase *)target);
	if (me == NULL || newService == NULL) {
		return true;
	}
	me->registerInterfaceWithBSD(newService);
	return true;
}

void
BCM2837DWC2::stop(IOService *provider)
{
	// DAR-352: no recovery may start, or still be running, against what the
	// rest of stop() is about to release.
	fStopping = true;
	if (fTestWedgeCall != NULL) {
		thread_call_cancel_wait(fTestWedgeCall);
	}
	if (fRecoveryCall != NULL) {
		thread_call_cancel_wait(fRecoveryCall);
	}

	// Undo start()'s manual attach()+start() of the Ethernet scaffolding
	// child, mirroring what IOService::terminate() would have done had
	// this gone through normal matching (stop() then detach()) - there was
	// no registerService()+match for it to begin with, so there is no
	// automatic teardown for it either.
	if (fInterfaceNotifier != NULL) {
		fInterfaceNotifier->remove();
		fInterfaceNotifier = NULL;
	}

	// Same reasoning as the Ethernet scaffolding below: attached and started
	// by hand in start(), so it has no automatic teardown either. Stopped
	// before the Ethernet driver because its UIM holds a pointer to this
	// controller and its poll timer must stop touching the hardware first.
	if (fUSBController != NULL) {
		fUSBController->stop(this);
		fUSBController->detach(this);
		fUSBController->release();
		fUSBController = NULL;
	}

	if (fEthernetDriver != NULL) {
		fEthernetDriver->stop(this);
		fEthernetDriver->detach(this);
		fEthernetDriver->release();
		fEthernetDriver = NULL;
	}

	if (fNetworkStack != NULL) {
		fNetworkStack->stop(this);
		fNetworkStack->detach(this);
		fNetworkStack->release();
		fNetworkStack = NULL;
	}

	if (fRegBase != NULL) {
		// Drop port power and mask everything, so a subsequent driver (or
		// a reboot into another OS) starts from a quiet controller.
		portWriteHPRT0(kHPRT0_PRTPWR, 0);
		regWrite(kDWC2_GINTMSK, 0);
		regWrite(kDWC2_GAHBCFG, 0);
	}
	// start() always registers the interrupt now (see its own DAR-316
	// comment - the dwc2-poll bisect gate that used to make this
	// conditional was removed once interrupt-driven completion was
	// exonerated as a cause).
	provider->unregisterInterrupt(0);
	super::stop(provider);
}

void
BCM2837DWC2::free(void)
{
	// Plain C++ object, not OSObject-derived (see SMSC9514USBTransport.h) -
	// deleted directly rather than release()'d. fEthernetDriver no longer
	// references it: stop() already tore that down first.
	delete fEthernetTransport;
	fEthernetTransport = NULL;

	if (fDMABuffer != NULL) {
		fDMABuffer->complete();
		fDMABuffer->release();
		fDMABuffer = NULL;
	}
	if (fAsyncDMABuffer != NULL) {
		fAsyncDMABuffer->complete();
		fAsyncDMABuffer->release();
		fAsyncDMABuffer = NULL;
	}
	fAsyncDMAVirt = NULL;
	fDMAVirt = NULL;
	if (fRegMap != NULL) {
		fRegMap->release();
		fRegMap = NULL;
	}
	fRegBase = NULL;
	if (fTransferLock != NULL) {
		IOLockFree(fTransferLock);
		fTransferLock = NULL;
	}
	if (fStatusLock != NULL) {
		IOSimpleLockFree(fStatusLock);
		fStatusLock = NULL;
	}
	if (fInterruptLock != NULL) {
		IOLockFree(fInterruptLock);
		fInterruptLock = NULL;
	}
	if (fBSDRegisterLock != NULL) {
		IOLockFree(fBSDRegisterLock);
		fBSDRegisterLock = NULL;
	}
	if (fTestWedgeCall != NULL) {
		thread_call_cancel_wait(fTestWedgeCall);
		thread_call_free(fTestWedgeCall);
		fTestWedgeCall = NULL;
	}
	if (fRecoveryCall != NULL) {
		thread_call_cancel_wait(fRecoveryCall);
		thread_call_free(fRecoveryCall);
		fRecoveryCall = NULL;
	}
	if (fEnumLock != NULL) {
		IOLockFree(fEnumLock);
		fEnumLock = NULL;
	}
	super::free();
}
