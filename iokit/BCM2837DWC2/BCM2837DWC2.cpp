/*
 * BCM2837 DWC2 USB 2.0 OTG host controller. See BCM2837DWC2.h for scope,
 * references, and known limitations - in particular that transfer
 * completion is polled rather than interrupt-driven in this first cut, on
 * purpose.
 */

#include "BCM2837DWC2.h"
#include "SMSC9514USBTransport.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/network/IONetworkStack.h>

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

// Only channel 0 is used - see BCM2837DWC2.h on why one channel, for now.
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
	 * Global interrupts stay MASKED (GLBLINTRMSK not set): completion is
	 * polled. See BCM2837DWC2.h's limitations note - this is deliberate
	 * and is the documented follow-up, not an oversight.
	 */

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
	fDMABuffer = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
	    kernel_task,
	    kIODirectionInOut | kIOMemoryPhysicallyContiguous |
	    kIOMapWriteCombineCache | kIOMemoryMapperNone,
	    kDWC2DataBufferSize,
	    0x3FFFFFFFULL & ~0x3ULL); // 4-byte aligned, DWC2 requires it

	if (fDMABuffer == NULL) {
		IOLog("BCM2837DWC2: DMA buffer allocation failed\n");
		return false;
	}

	if (fDMABuffer->prepare() != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: DMA buffer prepare() failed\n");
		return false;
	}

	fDMAVirt = fDMABuffer->getBytesNoCopy();

	IOByteCount segLen = 0;
	IOPhysicalAddress phys = fDMABuffer->getPhysicalSegment(0, &segLen, 0);
	if (phys == 0 || segLen < kDWC2DataBufferSize) {
		IOLog("BCM2837DWC2: DMA buffer not usable (phys=%#llx seglen=%llu)\n",
		    (uint64_t)phys, (uint64_t)segLen);
		return false;
	}

	fDMABusAddr = kBCM2837BusAddressBase | (uint64_t)phys;

	IOLog("BCM2837DWC2: DMA buffer virt=%p phys=%#llx bus=%#llx len=%u\n",
	    fDMAVirt, (uint64_t)phys, fDMABusAddr, kDWC2DataBufferSize);

	return true;
}

#pragma mark - Transfer engine

void
BCM2837DWC2::channelInit(DWC2Endpoint *ep, bool isIn, bool useSplit)
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

	regWrite(hcOffset(kDWC2Channel, kDWC2_HCCHAR), hcchar);

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
		regWrite(hcOffset(kDWC2Channel, kDWC2_HCSPLT),
		    kHCSPLT_SPLTENA |
		    ((uint32_t)ep->splitHubAddr << kHCSPLT_HUBADDR_SHIFT) |
		    ((uint32_t)ep->splitHubPort << kHCSPLT_PRTADDR_SHIFT));
	} else {
		regWrite(hcOffset(kDWC2Channel, kDWC2_HCSPLT), 0);
	}
}

bool
BCM2837DWC2::waitForChannelHalted(uint32_t chan, uint32_t timeoutMS, uint32_t *hcintOut)
{
	if (!waitForBits(hcOffset(chan, kDWC2_HCINT), kHCINT_CHHLTD, true, timeoutMS)) {
		if (hcintOut != NULL) {
			*hcintOut = regRead(hcOffset(chan, kDWC2_HCINT));
		}
		return false;
	}
	if (hcintOut != NULL) {
		*hcintOut = regRead(hcOffset(chan, kDWC2_HCINT));
	}
	return true;
}

/*
 * Wait out one armed chunk and decode what the silicon reports. doTransfer
 * has already programmed HCTSIZ/HCDMA and set CHEN; this only interprets the
 * result, so it deliberately takes no buffer or PID - those belong to the
 * caller, which owns the multi-chunk loop and the sequence bit.
 */
IOReturn
BCM2837DWC2::transferChunk(uint32_t xferLen, uint32_t *actualLen, uint32_t *hcintOut)
{
	*actualLen = 0;

	uint32_t hcint = 0;
	if (!waitForChannelHalted(kDWC2Channel, 2000, &hcint)) {
		IOLog("BCM2837DWC2: channel halt timeout (HCINT=%#x HCCHAR=%#x)\n",
		    hcint, regRead(hcOffset(kDWC2Channel, kDWC2_HCCHAR)));
		if (hcintOut != NULL) {
			*hcintOut = hcint;
		}
		return kIOReturnTimeout;
	}

	if (hcintOut != NULL) {
		*hcintOut = hcint;
	}

	uint32_t hctsiz = regRead(hcOffset(kDWC2Channel, kDWC2_HCTSIZ));
	uint32_t remaining = hctsiz & kHCTSIZ_XFERSIZE_MASK;

	if (hcint & kHCINT_XFERCOMP) {
		// HCTSIZ.XFERSIZE counts *down*: what is left is what was not
		// moved, so the delta is the real byte count. For an IN this is
		// how a short packet is detected.
		*actualLen = (remaining <= xferLen) ? (xferLen - remaining) : 0;
		return kIOReturnSuccess;
	}

	if (hcint & (kHCINT_NAK | kHCINT_FRMOVRUN | kHCINT_NYET)) {
		return kIOReturnNotReady; // caller retries; normal flow control
	}
	if (hcint & kHCINT_STALL) {
		return kIOReturnNotResponding; // endpoint stalled: real protocol error
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
	IOLog("BCM2837DWC2: transfer failed: %s (HCINT=%#x HCCHAR=%#x HCSPLT=%#x "
	      "HCTSIZ=%#x xferLen=%u)\n",
	    reason, hcint, regRead(hcOffset(kDWC2Channel, kDWC2_HCCHAR)),
	    regRead(hcOffset(kDWC2Channel, kDWC2_HCSPLT)), hctsiz, xferLen);

	return kIOReturnIOError;
}

IOReturn
BCM2837DWC2::doTransfer(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
                        void *buffer, uint32_t len, uint32_t *actualLen)
{
	if (fRegBase == NULL || fDMAVirt == NULL) {
		return kIOReturnNotReady;
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

	channelInit(ep, isIn, doSplit);

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
			ret = transferChunk(xferLen, &actual, &hcint);

			if (!(ret == kIOReturnIOError && (hcint & kHCINT_XACTERR))) {
				break;
			}
			if (attempt + 1 < 3) {
				IOLog("BCM2837DWC2: XACTERR on attempt %u/3, retrying\n",
				    attempt + 1);
			}
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
			uint32_t hctsiz = regRead(hcOffset(kDWC2Channel, kDWC2_HCTSIZ));
			*pid = (hctsiz & kHCTSIZ_PID_MASK) >> kHCTSIZ_PID_SHIFT;
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

	if (!hostInit()) {
		IOLog("BCM2837DWC2: host init failed\n");
		return false;
	}

	if (!portPowerOn()) {
		IOLog("BCM2837DWC2: port power-on failed\n");
		return false;
	}

	if (!portWaitForConnect(1000)) {
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

	if (!portReset()) {
		IOLog("BCM2837DWC2: root port reset failed\n");
		registerService();
		return true;
	}

	static const char *speedNames[] = { "high", "full", "low", "reserved" };
	IOLog("BCM2837DWC2: root port enabled, %s-speed device attached (HPRT0=%#x)\n",
	    speedNames[fRootSpeed & 3], regRead(kDWC2_HPRT0));

	// Walk the bus. A failure here leaves a working but empty controller
	// rather than failing to start - the difference matters for diagnosis,
	// since "controller up, enumeration failed" and "controller dead" have
	// completely different causes.
	enumerateBus();

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
					 * eth->start() above (attachInterface(), inside
					 * SMSC9514Ethernet::start()) only creates the
					 * IOEthernetInterface inside the IORegistry - it does
					 * NOT make it visible to BSD/ifconfig. On real macOS
					 * that last step is driven by a userspace daemon
					 * (configd's InterfaceNamer) opening a user client on
					 * IONetworkStack and naming the interface; this port
					 * has no configd. Both halves are done here by hand
					 * instead: construct the one IONetworkStack instance
					 * this kernel will ever have (nothing in this
					 * hand-built personality tree - BCM2837Personalities.cpp
					 * - ever matches or starts one; IOService::
					 * serviceMatching("IONetworkStack") only finds an
					 * *already-registered* instance, it does not create
					 * one), then call the same kernel-side naming path
					 * IOKitBSDInit.cpp's IORegisterNetworkInterface()
					 * already uses for the netboot-root-device case,
					 * generalized here to run for any real interface, not
					 * just that one. IONetworkStack::start() wires up an
					 * addMatchingNotification for IONetworkInterface
					 * publish events, which fires for interfaces already
					 * registered (ours) as well as future ones, so
					 * construction order relative to eth->start() does not
					 * matter - but IORegisterNetworkInterface() itself
					 * blocks in waitForService() until a matching
					 * IONetworkStack exists, so this stack must be started
					 * before that call, not after.
					 */
					IONetworkStack *stack = new IONetworkStack;
					if (stack != NULL && stack->init() && stack->attach(this)) {
						if (stack->start(this)) {
							fNetworkStack = stack;
							/*
							 * IORegisterNetworkInterface()'s argument must be
							 * the IONetworkInterface itself, not this
							 * controller - IONetworkStack::setProperties()
							 * resolves it back with
							 * OSDynamicCast(IONetworkInterface, ...), which
							 * fails (silently, just an error return) for a
							 * controller object. eth's fNetif is private for
							 * exactly this one caller - see
							 * getEthernetInterface()'s header comment.
							 */
							if (!IORegisterNetworkInterface(eth->getEthernetInterface())) {
								IOLog("BCM2837DWC2: IORegisterNetworkInterface failed - en0 will not appear\n");
							}
						} else {
							IOLog("BCM2837DWC2: IONetworkStack start() failed - en0 will not appear\n");
							stack->detach(this);
							stack->release();
						}
					} else {
						IOLog("BCM2837DWC2: IONetworkStack init/attach failed - en0 will not appear\n");
						if (stack != NULL) {
							stack->release();
						}
					}
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

	registerService();
	return true;
}

void
BCM2837DWC2::stop(IOService *provider)
{
	// Undo start()'s manual attach()+start() of the Ethernet scaffolding
	// child, mirroring what IOService::terminate() would have done had
	// this gone through normal matching (stop() then detach()) - there was
	// no registerService()+match for it to begin with, so there is no
	// automatic teardown for it either.
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
	super::free();
}
