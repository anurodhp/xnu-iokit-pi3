/*
 * Driver for the Synopsys DesignWare USB 2.0 OTG ("dwc2") host controller,
 * the BCM2837's only USB silicon. DT node "usb" in devicetrees/rpi3.dts
 * (Linux/u-boot: usb@7e980000, "brcm,bcm2835-usb").
 *
 * ============================== WHY / SCOPE ==============================
 *
 * This is the gate on all of Phase 6 networking: the Pi 3's Ethernet is an
 * SMSC LAN9514, a USB device hanging off this controller, so not one packet
 * can move until this driver enumerates it (docs/usb-support-plan.md).
 *
 * Deliberately NOT an IOUSBFamily IOUSBController subclass. That option was
 * weighed in docs/usb-support-plan.md and rejected: Apple's own host
 * controller drivers in apple-oss-distributions/IOUSBFamily run 3300-6400
 * lines apiece, most of it satisfying IOUSBController's abstract surface
 * (isochronous scheduling, the full power-management state machine) that a
 * single soldered-on Ethernet chip will never exercise. There is also no
 * DWC2 driver in that family to start from - Apple never open-sourced their
 * equivalent for this IP block - so subclassing would buy compatibility
 * with hub/HID drivers that this tree does not have either. Instead this is
 * a purpose-built stack exposing exactly the primitives above it need, the
 * same call this project already made for SMSCUSBTransport.
 *
 * ============================= REFERENCES ===============================
 *
 * Ported from third_party/u-boot/drivers/usb/host/dwc2.c (1375 lines), the
 * primary target: it is already in-tree, and already drives this exact
 * silicon in this project's own boot chain (u-boot brings USB up before
 * handing off to XNU), so it is a known-good register sequence for BCM2837
 * rather than a generic one. Register bit definitions below were taken from
 * its dwc2.h and cross-checked against Linux's drivers/usb/dwc2/hw.h (via
 * the copy vendored for the QEMU model at
 * third_party/qemu-zhuowei/include/hw/usb/dwc2-regs.h); FreeBSD's
 * sys/dev/usb/controller/dwc_otg.c was consulted for the split-transaction
 * state machine, where u-boot's version is the terser of the two.
 *
 * Divergences from u-boot, all deliberate:
 *   - u-boot hardcodes one host channel (DWC2_HC_CHANNEL 0) and runs every
 *     transfer synchronously. Kept, for now: the silicon has 8 channels,
 *     but nothing here issues concurrent transfers yet, and a one-channel
 *     driver is far easier to reason about. Revisit when Ethernet RX wants
 *     to overlap with TX.
 *   - u-boot's root-hub emulation (dwc_otg_submit_rh_msg*, ~270 lines that
 *     synthesize USB hub descriptors for the root port so u-boot's generic
 *     USB core can walk it uniformly) is dropped entirely. There is no
 *     generic USB core here to satisfy; this driver drives the root port's
 *     HPRT0 register directly.
 *
 * ======================= KNOWN LIMITATIONS (honest) =====================
 *
 *   - UPDATE (DAR-136, commit 6b58912): channel-completion waits are now
 *     real interrupt-driven (interruptOccurred() / waitForChannelInt()),
 *     with a direct-register-poll fallback for the case no interrupt lands
 *     within one tick - see fInterruptLock's own header comment for the
 *     full design (modeled on BCM2837SDHCI's fInterruptLock/fStatusLock
 *     split). GAHBCFG.GLBLINTRMSK is set in start(), after the handler is
 *     registered. The paragraph this replaced (which said completion was
 *     purely polled and GLBLINTRMSK was left clear) described the
 *     first-cut bring-up and is no longer true of this file - left here,
 *     corrected rather than deleted, because DAR-351 found
 *     SMSC9514Ethernet.h still citing the old version of this section by
 *     name; don't let that happen to *this* correction too.
 *   - What DAR-136 did NOT change, and what still bounds real RX latency
 *     (DAR-351): this driver still drives exactly ONE host channel
 *     (kDWC2Channel) for every transfer, RX and TX alike (see the
 *     "Divergences from u-boot" note above). A bulk-IN endpoint has no way
 *     to tell the host "data is ready" other than the host asking via an
 *     IN token, so SMSC9514Ethernet's RX side still has to periodically
 *     re-arm a new bulk-IN listen (fRxTimer) rather than sit on one
 *     already-armed request forever - continuous back-to-back re-arming
 *     would starve TX and link-status polling of the same shared channel
 *     for as long as RX held it. DAR-136 made each individual transaction's
 *     completion wait fast (sub-millisecond once armed, real interrupt
 *     latency rather than a register-poll loop); it did not and could not
 *     remove the RX-vs-TX channel-sharing gap between arms, since that is
 *     a single-channel design choice, not a completion-detection one. The
 *     real fix for that remaining gap is a second host channel dedicated
 *     to RX (the silicon has up to 8 - see fNumChannels), so TX/link-status
 *     can use channel 0 without waiting on an outstanding RX listen. Not
 *     done: unverifiable in QEMU (LAN9514 is never emulated - see
 *     reference_qemu_lan9514_never_emulated) and real-hardware time was not
 *     available this session (DAR-351).
 *   - Isochronous transfers are unimplemented and will not be: no consumer.
 *   - One transfer at a time on the single active channel, no queueing
 *     across channels, no cross-channel async completion callbacks.
 */

#ifndef _BCM2837_DWC2_H
#define _BCM2837_DWC2_H

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

/*
 * Register offsets, bytes from the controller base. Matches u-boot's
 * struct dwc2_core_regs layout (dwc2.h) and the QEMU model's own
 * hcd-dwc2.h; both agree, which is a useful cross-check since one is a
 * driver and the other an independent reimplementation of the silicon.
 */
enum {
	kDWC2_GOTGCTL   = 0x000,
	kDWC2_GAHBCFG   = 0x008,
	kDWC2_GUSBCFG   = 0x00c,
	kDWC2_GRSTCTL   = 0x010,
	kDWC2_GINTSTS   = 0x014,
	kDWC2_GINTMSK   = 0x018,
	kDWC2_GRXFSIZ   = 0x024,
	kDWC2_GNPTXFSIZ = 0x028,
	kDWC2_GSNPSID   = 0x040,
	kDWC2_GHWCFG1   = 0x044,
	kDWC2_GHWCFG2   = 0x048,
	kDWC2_GHWCFG3   = 0x04c,
	kDWC2_GHWCFG4   = 0x050,

	kDWC2_HPTXFSIZ  = 0x100,

	kDWC2_HCFG      = 0x400,
	kDWC2_HFIR      = 0x404,
	kDWC2_HFNUM     = 0x408,
	kDWC2_HAINT     = 0x414,
	kDWC2_HAINTMSK  = 0x418,
	kDWC2_HPRT0     = 0x440,

	// Per-host-channel block: base + channel * stride.
	kDWC2_HCCHAR_BASE = 0x500,
	kDWC2_HC_STRIDE   = 0x020,
	kDWC2_HCCHAR      = 0x000,
	kDWC2_HCSPLT      = 0x004,
	kDWC2_HCINT       = 0x008,
	kDWC2_HCINTMSK    = 0x00c,
	kDWC2_HCTSIZ      = 0x010,
	kDWC2_HCDMA       = 0x014,

	kDWC2_PCGCCTL   = 0xe00,
};

enum {
	kGOTGCTL_HSTSETHNPEN = 1U << 10,

	kGAHBCFG_GLBLINTRMSK  = 1U << 0,
	kGAHBCFG_HBURSTLEN_INCR4 = 3U << 1,
	kGAHBCFG_DMAENABLE    = 1U << 5,

	kGUSBCFG_PHYIF          = 1U << 3,
	kGUSBCFG_ULPI_UTMI_SEL  = 1U << 4,
	kGUSBCFG_SRPCAP         = 1U << 8,
	kGUSBCFG_HNPCAP         = 1U << 9,
	kGUSBCFG_ULPI_FSLS      = 1U << 17,
	kGUSBCFG_ULPI_CLK_SUS_M = 1U << 19,
	kGUSBCFG_ULPI_EXT_VBUS_DRV = 1U << 20,
	kGUSBCFG_TERM_SEL_DL_PULSE = 1U << 22,
	kGUSBCFG_FORCEHOSTMODE  = 1U << 29,

	kGRSTCTL_CSFTRST    = 1U << 0,
	kGRSTCTL_RXFFLSH    = 1U << 4,
	kGRSTCTL_TXFFLSH    = 1U << 5,
	kGRSTCTL_TXFNUM_SHIFT = 6,
	kGRSTCTL_AHBIDLE    = 1U << 31,

	kGINTSTS_CURMODE_HOST = 1U << 0,
	// Real bit position confirmed against QEMU's own real dwc2-regs.h
	// (third_party/qemu-zhuowei/include/hw/usb/dwc2-regs.h:
	// `#define GINTSTS_HCHINT BIT(25)`), same source this whole class
	// already cross-checks its other real register bits against.
	kGINTSTS_HCHINT     = 1U << 25,

	kHWCFG2_ARCHITECTURE_MASK  = 0x3U << 3,
	kHWCFG2_ARCHITECTURE_SHIFT = 3,
	kHWCFG2_ARCH_SLAVE_ONLY    = 0,
	kHWCFG2_ARCH_EXT_DMA       = 1,
	kHWCFG2_ARCH_INT_DMA       = 2,
	kHWCFG2_NUM_HOST_CHAN_MASK  = 0xFU << 14,
	kHWCFG2_NUM_HOST_CHAN_SHIFT = 14,
	kHWCFG2_DYNAMIC_FIFO        = 1U << 19,

	kFIFOSIZE_STARTADDR_SHIFT = 0,
	kFIFOSIZE_DEPTH_SHIFT     = 16,

	kHCFG_FSLSPCLKSEL_MASK      = 0x3U << 0,
	kHCFG_FSLSPCLKSEL_30_60_MHZ = 0,
	kHCFG_FSLSPCLKSEL_48_MHZ    = 1,

	kHFNUM_MAX_FRNUM = 0x3FFF,

	kHPRT0_PRTCONNSTS   = 1U << 0,
	kHPRT0_PRTCONNDET   = 1U << 1,
	kHPRT0_PRTENA       = 1U << 2,
	kHPRT0_PRTENCHNG    = 1U << 3,
	kHPRT0_PRTOVRCURRACT = 1U << 4,
	kHPRT0_PRTOVRCURRCHNG = 1U << 5,
	kHPRT0_PRTRST       = 1U << 8,
	kHPRT0_PRTPWR       = 1U << 12,
	kHPRT0_PRTSPD_MASK  = 0x3U << 17,
	kHPRT0_PRTSPD_SHIFT = 17,
	kHPRT0_PRTSPD_HIGH  = 0,
	kHPRT0_PRTSPD_FULL  = 1,
	kHPRT0_PRTSPD_LOW   = 2,
	// HPRT0 bits that are write-1-to-clear. Every read-modify-write of
	// HPRT0 must mask these off or it silently acks a change it never
	// handled - a classic DWC2 bug, and the reason portWriteHPRT0()
	// exists rather than open-coding the read/modify/write.
	kHPRT0_W1C_MASK = kHPRT0_PRTCONNDET | kHPRT0_PRTENCHNG |
	                  kHPRT0_PRTOVRCURRCHNG | kHPRT0_PRTENA,

	kHCCHAR_MPS_MASK      = 0x7FFU << 0,
	kHCCHAR_EPNUM_SHIFT   = 11,
	kHCCHAR_EPDIR         = 1U << 15,
	kHCCHAR_LSPDDEV       = 1U << 17,
	kHCCHAR_EPTYPE_SHIFT  = 18,
	kHCCHAR_MULTICNT_MASK = 0x3U << 20,
	kHCCHAR_MULTICNT_SHIFT = 20,
	kHCCHAR_DEVADDR_SHIFT = 22,
	kHCCHAR_ODDFRM        = 1U << 29,
	kHCCHAR_CHDIS         = 1U << 30,
	kHCCHAR_CHEN          = 1U << 31,

	kHCCHAR_EPTYPE_CONTROL = 0,
	kHCCHAR_EPTYPE_ISOC    = 1,
	kHCCHAR_EPTYPE_BULK    = 2,
	kHCCHAR_EPTYPE_INTR    = 3,

	kHCSPLT_PRTADDR_SHIFT = 0,
	kHCSPLT_HUBADDR_SHIFT = 7,
	kHCSPLT_COMPSPLT      = 1U << 16,
	kHCSPLT_SPLTENA       = 1U << 31,

	kHCINT_XFERCOMP  = 1U << 0,
	kHCINT_CHHLTD    = 1U << 1,
	kHCINT_AHBERR    = 1U << 2,
	kHCINT_STALL     = 1U << 3,
	kHCINT_NAK       = 1U << 4,
	kHCINT_ACK       = 1U << 5,
	kHCINT_NYET      = 1U << 6,
	kHCINT_XACTERR   = 1U << 7,
	kHCINT_BBLERR    = 1U << 8,
	kHCINT_FRMOVRUN  = 1U << 9,
	kHCINT_DATATGLERR = 1U << 10,
	kHCINT_ALL       = 0x3FFF,

	kHCTSIZ_XFERSIZE_MASK = 0x7FFFF,
	kHCTSIZ_PKTCNT_SHIFT  = 19,
	kHCTSIZ_PID_SHIFT     = 29,
	kHCTSIZ_PID_MASK      = 0x3U << 29,

	// DATA toggle / SETUP encoding in HCTSIZ.PID. Note DATA2 and DATA1
	// are NOT in numeric order - this is the silicon's encoding, not a
	// typo (u-boot dwc2.h:742).
	kHCPID_DATA0 = 0,
	kHCPID_DATA2 = 1,
	kHCPID_DATA1 = 2,
	kHCPID_MDATA = 3,
	kHCPID_SETUP = 3,
};

// USB-spec constants (hand-rolled rather than pulled from IOUSBFamily's
// headers, which drag in the IOTypes.h -> mach_interface.h SDK header skew
// documented in docs/usb-support-plan.md's "Compile/test verification
// caveat"). Values are from the USB 2.0 specification, chapter 9.
enum {
	kUSBReqDirIn  = 0x80,
	kUSBReqDirOut = 0x00,
	kUSBReqTypeStandard = 0x00,
	kUSBReqTypeClass    = 0x20,
	kUSBReqTypeVendor   = 0x40,
	kUSBReqRecipDevice    = 0x00,
	kUSBReqRecipInterface = 0x01,
	kUSBReqRecipEndpoint  = 0x02,
	kUSBReqRecipOther     = 0x03,

	kUSBReqGetStatus     = 0,
	kUSBReqClearFeature  = 1,
	kUSBReqSetFeature    = 3,
	kUSBReqSetAddress    = 5,
	kUSBReqGetDescriptor = 6,
	kUSBReqGetConfiguration = 8,
	kUSBReqSetConfiguration = 9,

	kUSBDescDevice    = 1,
	kUSBDescConfig    = 2,
	kUSBDescString    = 3,
	kUSBDescInterface = 4,
	kUSBDescEndpoint  = 5,
	kUSBDescHub       = 0x29,
};

// Speeds, in this driver's own numbering (HPRT0.PRTSPD encoding).
enum {
	kUSBSpeedHigh = 0,
	kUSBSpeedFull = 1,
	kUSBSpeedLow  = 2,
};

#pragma pack(push, 1)
struct USBSetupPacket {
	uint8_t  bmRequestType;
	uint8_t  bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

struct USBDeviceDescriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdUSB;
	uint8_t  bDeviceClass;
	uint8_t  bDeviceSubClass;
	uint8_t  bDeviceProtocol;
	uint8_t  bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t  iManufacturer;
	uint8_t  iProduct;
	uint8_t  iSerialNumber;
	uint8_t  bNumConfigurations;
};

struct USBConfigDescriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wTotalLength;
	uint8_t  bNumInterfaces;
	uint8_t  bConfigurationValue;
	uint8_t  iConfiguration;
	uint8_t  bmAttributes;
	uint8_t  bMaxPower;
};

struct USBInterfaceDescriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
};

struct USBEndpointDescriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bEndpointAddress;
	uint8_t  bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t  bInterval;
};

struct USBHubDescriptor {
	uint8_t  bDescLength;
	uint8_t  bDescriptorType;
	uint8_t  bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t  bPwrOn2PwrGood;
	uint8_t  bHubContrCurrent;
	// Variable-length DeviceRemovable/PortPwrCtrlMask follow; unused here.
	uint8_t  varData[8];
};
#pragma pack(pop)

// Hub class feature selectors (USB 2.0 spec 11.24.2).
enum {
	kHubFeatPortConnection  = 0,
	kHubFeatPortEnable      = 1,
	kHubFeatPortReset       = 4,
	kHubFeatPortPower       = 8,
	kHubFeatCPortConnection = 16,
	kHubFeatCPortEnable     = 17,
	kHubFeatCPortSuspend    = 18,
	kHubFeatCPortOverCurrent = 19,
	kHubFeatCPortReset      = 20,
};

// wPortStatus bits (USB 2.0 spec Table 11-21).
enum {
	kPortStatusConnection = 1U << 0,
	kPortStatusEnable     = 1U << 1,
	kPortStatusReset      = 1U << 4,
	kPortStatusPower      = 1U << 8,
	kPortStatusLowSpeed   = 1U << 9,
	kPortStatusHighSpeed  = 1U << 10,
};

/*
 * wPortChange bits (USB 2.0 spec Table 11-22). These are DELIBERATELY
 * separate from the kHubFeat* selectors above, because the two numbering
 * schemes are different and conflating them is a silent bug: C_PORT_RESET
 * is feature selector 20 (what ClearPortFeature takes) but wPortChange
 * BIT 4 (what a GET_STATUS reply carries). Testing `change & (1 << 20)`
 * against a 16-bit wPortChange can never be true - which is exactly what
 * this driver did, so the reset-complete check silently never fired and
 * only the status-based fallback beside it kept enumeration working.
 * Values cross-checked against u-boot's include/usb_defs.h
 * (USB_PORT_STAT_C_RESET == 0x0010).
 */
enum {
	kPortChangeConnection  = 1U << 0,
	kPortChangeEnable      = 1U << 1,
	kPortChangeSuspend     = 1U << 2,
	kPortChangeOverCurrent = 1U << 3,
	kPortChangeReset       = 1U << 4,
};

/*
 * Everything this driver needs to address one endpoint on one device. The
 * caller owns these; the driver keeps no device table of its own (there is
 * no generic USB core here to own one).
 *
 * `toggle` is the DATA0/DATA1 sequence bit, which USB requires be tracked
 * per-endpoint across transfers - so it lives here rather than in the
 * driver, which is stateless between calls.
 *
 * splitHubAddr/splitHubPort are the "hub-in-the-path" case: a full- or
 * low-speed device sitting behind a high-speed hub needs its transactions
 * wrapped in SPLIT/CSPLIT, addressed to the hub's own address and downstream
 * port number. This is not optional on this board - the LAN9514's Ethernet
 * function is exactly such a device (docs/usb-support-plan.md).
 */
struct DWC2Endpoint {
	uint8_t  devAddr;
	uint8_t  epNum;
	bool     epIsIn;
	uint8_t  epType;      // kHCCHAR_EPTYPE_*
	uint16_t maxPacket;
	uint8_t  speed;       // kUSBSpeed*
	uint8_t  toggle;      // kHCPID_DATA0 / kHCPID_DATA1
	uint8_t  splitHubAddr; // 0 = no split
	uint8_t  splitHubPort;
};

/*
 * One enumerated device. This is the whole "USB core" this stack has: a
 * flat table of what was found, with just the endpoints a consumer here
 * actually uses. No interface/alt-setting model, no device driver matching -
 * see BCM2837DWC2.h's scope note on why a generic family was not built.
 */
struct DWC2USBDevice {
	bool     inUse;
	uint8_t  address;
	uint8_t  speed;
	uint16_t maxPacket0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint8_t  deviceClass;
	uint8_t  interfaceClass;
	uint8_t  configValue;
	// Set when this device sits behind a high-speed hub and therefore
	// needs SPLIT transactions; 0 means directly on the root port.
	uint8_t  splitHubAddr;
	uint8_t  splitHubPort;
	// Which hub port this was found on (0 = root port), for logging and
	// for addressing the hub when re-resetting.
	uint8_t  parentHubAddr;
	uint8_t  parentPort;
	// First bulk/interrupt endpoint of each direction, if any. 0 = absent.
	uint8_t  bulkInEp;
	uint8_t  bulkOutEp;
	uint8_t  intrInEp;
	uint16_t bulkInMax;
	uint16_t bulkOutMax;
	uint16_t intrInMax;
};

#define kDWC2MaxDevices 8

// Forward-declared rather than #included here so that BCM2837DWC2.h - a
// chip-agnostic host controller header - does not pull in the SMSC-specific
// classes just to hold two owned pointers to them. Both are only ever
// dereferenced from BCM2837DWC2.cpp, which does include their real headers.
class SMSC9514USBTransport;
class SMSC9514Ethernet;
class IONetworkStack;
class BCM2837USBHostController;

class BCM2837DWC2 : public IOService
{
	OSDeclareDefaultStructors(BCM2837DWC2);

private:
	IOMemoryMap             *fRegMap;
	volatile uint8_t        *fRegBase;
	IOBufferMemoryDescriptor *fDMABuffer;
	void                    *fDMAVirt;
	uint64_t                 fDMABusAddr;   // VC bus address for HCDMA
	uint32_t                 fNumChannels;
	uint8_t                  fRootSpeed;
	bool                     fRootConnected;

	/*
	 * Serializes every call into transfer()/controlTransfer() - both
	 * ultimately drive the single shared hardware channel (kDWC2Channel)
	 * and the single shared fDMAVirt staging buffer via doTransfer(), and
	 * "Revisit when Ethernet RX wants to overlap with TX" (this file's own
	 * header comment, written before RX existed) is now live:
	 * SMSC9514Ethernet's fRxTimer calls transfer() from its IOWorkLoop's
	 * timer callout while outputPacket() calls it synchronously from
	 * whatever arbitrary thread ifnet_output() runs on (IONetworkController
	 * has no output queue here - createOutputQueue() was never overridden,
	 * and this project's IONetworkingFamily vendoring has no
	 * IOBasicOutputQueue.cpp/IOGatedOutputQueue.cpp to override it with -
	 * so outputPacket() is never gated onto the workloop at all). Without
	 * this lock those two genuinely run concurrently on two different
	 * threads with no serialization at all, confirmed by reading both call
	 * paths, not just suspected. Diagnosed (not yet register-trace-
	 * confirmed - no real hardware debugger available this session) as the
	 * cause of a real-hardware symptom: `ping` intermittently reporting
	 * "sendto: Operation not permitted" on roughly every other echo
	 * request while every sequence number still gets a real reply with no
	 * gaps - consistent with the RX poll's concurrent use of the same
	 * channel/HCINT registers corrupting doTransfer()'s own completion-
	 * status readback for a TX transfer that genuinely still reached the
	 * wire, rather than with any real permission check or genuine
	 * transmit failure (grepped the real xnu-7195 network stack for every
	 * synchronous EPERM site reachable from sendto() on a raw ICMP socket;
	 * none fit an intermittent, always-recovers pattern).
	 */
	IOLock                  *fTransferLock;

	/*
	 * Interrupt-driven channel-completion state (DAR-136), same hybrid
	 * shape as BCM2837SDHCI's fInterruptLock/fStatusLock/fPendingIntStatus
	 * (see that class's own header/interruptOccurred()/waitForIntStatus32()
	 * for the full real story this is modeled on): fStatusLock is an
	 * IOSimpleLock (a real spinlock, safe to take from interrupt level,
	 * where IOLockLock/IOLockSleep are documented as unsafe) guarding
	 * fPendingHcInt; fInterruptLock is a real IOLock only ever touched from
	 * thread context, needed because IOLockSleepDeadline() requires a
	 * lockable IOLock to atomically unlock-and-sleep on. IOLockWakeup()
	 * bridges the two without itself taking fInterruptLock (a bare
	 * thread_wakeup_prim(), confirmed safe at interrupt level in xnu's
	 * IOLocks.cpp, same as SDHCI's own comment documents).
	 *
	 * Scoped to ONE channel deliberately, matching this whole driver's
	 * existing one-channel-at-a-time design (see the class header's own
	 * "u-boot hardcodes one host channel... kept, for now" note) --
	 * fPendingHcInt accumulates whatever HCINT bits interruptOccurred()/
	 * the poll fallback in waitForChannelInt() have observed for
	 * kDWC2Channel since the last consumer drained it, not a per-channel
	 * array. Revisit alongside that same "nothing here issues concurrent
	 * transfers yet" scope note if this ever grows past one channel.
	 */
	IOLock                  *fInterruptLock;
	IOSimpleLock            *fStatusLock;
	uint32_t                 fPendingHcInt;
	uint32_t                 fRealInterruptHits;
	uint32_t                 fPollFallbackHits;
	bool                     fLoggedRealInterrupt;
	bool                     fLoggedPollFallback;

	/*
	 * DAR-352: real hardware, real, reproduced-twice failure -
	 * haltChannel()'s forced CHEN+CHDIS write can fail to complete even
	 * after retrying (see haltChannel()'s own updated comment), leaving
	 * the single shared host channel permanently unusable. Once that is
	 * confirmed, the channel must never be touched again - re-arming a
	 * channel whose CHENA never really cleared is not a real
	 * disable->enable edge (this whole file's running theme, e.g. DAR-282/
	 * DAR-290's own comments), so retrying forever just re-discovers the
	 * same stuck state every timeout period, forever, flooding the console
	 * and leaving the whole board looking hung (confirmed on real hardware
	 * - see DAR-352). fChannelWedged is checked once, centrally, at the
	 * top of doTransfer() (every public entry point funnels through it
	 * under fTransferLock, already held on every path that could set or
	 * read this), so setting it makes every future transfer() /
	 * controlTransfer() / transferStage() call fail instantly and cleanly
	 * (kIOReturnNotAttached) instead of touching hardware again. No
	 * separate lock: guarded by fTransferLock like every other field
	 * doTransfer()/haltChannel() touch, and every read/write of it already
	 * happens with that lock held. Relies on this class's normal
	 * zero-initialization (same as fPendingHcInt/fRealInterruptHits/
	 * fLoggedRealInterrupt above, none of which get an explicit `= 0`/
	 * `= false` in start() either) rather than an explicit init line, for
	 * consistency with those sibling fields.
	 *
	 * This does NOT recover the controller - that needs a real core
	 * reset + host re-init + root-port re-bring-up + bus re-enumeration,
	 * scoped but not implemented (DAR-352's own "full fix"). This is only
	 * the "stop hammering, fail cleanly" half: it keeps the rest of the
	 * board (console, unrelated subsystems) responsive after a wedge
	 * instead of an endless retry storm, but real recovery still needs a
	 * physical power cycle until the full fix lands.
	 */
	bool                     fChannelWedged;

	/*
	 * DAR-297: registerInterfaceWithBSD() can be reached from two
	 * independent paths -- the direct call in start()'s real-hardware
	 * SMSC9514Ethernet branch, and interfacePublished(), an IOKit publish
	 * notification callback that this same start() call installs and
	 * that the class header comment on registerInterfaceWithBSD() itself
	 * documents as asynchronous relative to it. Its own getProperty(
	 * gIOBSDNameKey) check-then-IORegisterNetworkInterface() sequence is
	 * idempotent against a second SEQUENTIAL call, but not against two
	 * CONCURRENT ones -- both can observe the property unset before
	 * either has set it. A real IOLock closes that TOCTOU window; a
	 * dedicated lock rather than reusing fTransferLock or fInterruptLock
	 * because this guards BSD/IORegistry state, not the USB channel or
	 * interrupt state either of those already serialize.
	 */
	IOLock                  *fBSDRegisterLock;

	// Bring-up scaffolding (see the long comment at the call site in
	// start()): owned for as long as this controller is, so the Ethernet
	// driver they back has a live transport past start() returning. NULL
	// on any board/boot where no SMSC part enumerated (every QEMU run).
	SMSC9514USBTransport     *fEthernetTransport;
	SMSC9514Ethernet         *fEthernetDriver;

	// The one IONetworkStack instance this kernel ever creates - see the
	// long comment at the call site in start(). Nothing else in this
	// hand-built personality tree (BCM2837Personalities.cpp) ever matches
	// or starts an IONetworkStack, so without this, IOKitBSDInit.cpp's
	// IORegisterNetworkInterface() would block forever in
	// waitForService(serviceMatching("IONetworkStack")).
	IONetworkStack           *fNetworkStack;

	/*
	 * The real IOUSBFamily controller layered over this driver, and the
	 * notification that completes BSD registration for any IONetworkInterface
	 * published beneath it. Both NULL if the controller never came up - see
	 * startUSBHostController(). Owned here: this class attach()/start()s the
	 * controller by hand, so nothing else will ever tear it down.
	 */
	BCM2837USBHostController *fUSBController;
	IONotifier               *fInterfaceNotifier;

	void startUSBHostController(void);
	void registerInterfaceWithBSD(IOService *netif);
	static bool interfacePublished(void *target, void *ref, IOService *newService,
	                               IONotifier *notifier);

	// Register accessors. All DWC2 registers are 32-bit.
	uint32_t regRead(uint32_t offset) const;
	void     regWrite(uint32_t offset, uint32_t value);
	void     regModify(uint32_t offset, uint32_t clearBits, uint32_t setBits);
	uint32_t hcOffset(uint32_t chan, uint32_t reg) const;

	// Poll `offset` until (value & mask) == (set ? mask : 0), or timeout.
	bool waitForBits(uint32_t offset, uint32_t mask, bool set, uint32_t timeoutMS);
	// waitForBits() wants every bit in the mask; this one wants any of them.
	bool waitForAnyBits(uint32_t offset, uint32_t mask, uint32_t timeoutMS);

	// Real interrupt handler (DAR-136) -- see fInterruptLock's own header
	// comment. Registered against the provider in start(), same call shape
	// as BCM2837SDHCI::interruptOccurred().
	static void interruptOccurred(OSObject *target, void *refCon,
	    IOService *nub, int source);
	// Hybrid interrupt-or-poll wait for `chan`'s HCINT to have ANY bit in
	// `mask` set, real interrupt wins immediately via IOLockWakeup(), a
	// direct register poll is the safety net (same shape as
	// BCM2837SDHCI::waitForIntStatus32() -- see fInterruptLock's own
	// header comment). *hcintOut gets the FULL accumulated HCINT bit
	// pattern observed (not just the mask-matched subset), matching what
	// waitForChannelSettled()'s real callers need (XFERCOMP/NAK/STALL/
	// error bits, not only CHHLTD).
	bool waitForChannelInt(uint32_t chan, uint32_t mask, uint32_t timeoutMS,
	    uint32_t *hcintOut);

	// Core bring-up, ported from u-boot's dwc_otg_core_init/
	// dwc_otg_core_host_init.
	bool coreReset(void);
	bool flushTxFIFO(uint32_t num);
	bool flushRxFIFO(void);
	bool coreInit(void);
	bool hostInit(void);

	// Root port.
	void portWriteHPRT0(uint32_t clearBits, uint32_t setBits);
	bool portPowerOn(void);
	bool portWaitForConnect(uint32_t timeoutMS);
	bool portReset(void);

	bool allocateDMABuffer(void);

	// Wait out one already-armed chunk and decode the result. Takes no
	// buffer or PID: doTransfer owns those and the multi-chunk loop.
	IOReturn transferChunk(uint32_t xferLen, bool isIn, uint32_t *actualLen, uint32_t *hcintOut);

	// Wait for a halt OR an un-halted NAK, and force a still-enabled channel
	// down. See the implementations for why a NAKed non-periodic channel does
	// not halt itself, and why leaving one enabled silently wedges every
	// other endpoint on this single-channel controller.
	bool waitForChannelSettled(uint32_t chan, uint32_t timeoutMS, uint32_t *hcintOut);
	void haltChannel(uint32_t chan, uint32_t *hcintOut);

	void channelInit(DWC2Endpoint *ep, bool isIn, bool useSplit);

	/*
	 * The real workhorse; transfer() and controlTransfer() are both thin
	 * wrappers. `pid` is in/out - a control transfer drives it with its
	 * own per-stage value (SETUP, then DATA1...) while a bulk/interrupt
	 * transfer passes &ep->toggle so the sequence bit persists across
	 * calls, which is what USB requires of a data endpoint.
	 */
	IOReturn doTransfer(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
	                    void *buffer, uint32_t len, uint32_t *actualLen);

	// doTransfer plus bounded NAK retry. A device NAKing is normal flow
	// control, not an error, so it must not surface as one.
	IOReturn doTransferRetrying(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
	                            void *buffer, uint32_t len, uint32_t *actualLen,
	                            uint32_t timeoutMS);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	/*
	 * Move up to `len` bytes on `ep`. Splits into channel-sized chunks
	 * internally and updates ep->toggle. Returns kIOReturnSuccess only on
	 * full or short-but-clean completion; *actualLen gets bytes moved.
	 * `timeoutMS` bounds the NAK-retry loop (doTransferRetrying) - default
	 * 1000ms matches every pre-existing caller. A short-poll caller like
	 * an RX poll loop, where "device NAKed the whole time" just means "no
	 * packet waiting yet" and is the common case, should pass something
	 * much smaller so it doesn't block its workloop for a full second on
	 * every idle poll.
	 */
	IOReturn transfer(DWC2Endpoint *ep, void *buffer, uint32_t len, uint32_t *actualLen,
	                  uint32_t timeoutMS = 1000);

	/*
	 * Full control transfer: SETUP, optional DATA, STATUS. `ep` must
	 * describe endpoint 0 of the target device. Direction and length come
	 * from `setup` itself.
	 */
	IOReturn controlTransfer(DWC2Endpoint *ep, const USBSetupPacket *setup,
	                         void *data, uint32_t *actualLen);

	// Convenience wrapper for the common case.
	IOReturn controlRequest(DWC2Endpoint *ep, uint8_t bmRequestType, uint8_t bRequest,
	                        uint16_t wValue, uint16_t wIndex, uint16_t wLength,
	                        void *data, uint32_t *actualLen);

	uint8_t  rootPortSpeed(void) const { return fRootSpeed; }
	bool     rootPortConnected(void) const { return fRootConnected; }

	/*
	 * --- Primitives for an IOUSBFamily UIM (BCM2837USBHostController) ---
	 *
	 * transfer()/controlTransfer() above are complete, blocking operations:
	 * they own the data toggle and the SETUP/DATA/STATUS sequence, and they
	 * retry NAKs internally for up to a second. That is the right shape for
	 * this driver's own hand-wired consumers, and the wrong shape entirely
	 * for a UIM, which must queue asynchronous work, drive control transfers
	 * one stage at a time (IOUSBController::ControlTransaction issues SETUP,
	 * DATA and STATUS as three separate UIMCreateControlTransfer calls), and
	 * never block its workloop waiting out a NAK.
	 *
	 * transferStage() is that lower-level primitive: one bounded attempt on
	 * one endpoint with a caller-owned PID. It is a thin public wrapper over
	 * the existing private doTransferRetrying(), taking the same
	 * fTransferLock, so there is exactly one path into the shared channel and
	 * DMA buffer - not a second, parallel transfer engine.
	 *
	 * `pid` is in/out and caller-owned on purpose. A control stage drives it
	 * with that stage's own value (SETUP, then DATA1); a bulk or interrupt
	 * transfer passes &ep->toggle so the sequence bit persists across
	 * transfers, which is what USB requires of a data endpoint.
	 *
	 * `timeoutMS` bounds the NAK-retry loop. A UIM passes 0 - one attempt,
	 * then tell me - because it owns retrying itself and a NAK means "not
	 * yet", not "failed". kIOReturnTimeout with *actualLen advanced means
	 * partial progress: resume from there rather than restarting, or the
	 * already-advanced data toggle will make the device silently drop the
	 * re-sent packets as duplicates (see doTransferRetrying's own comment).
	 */
	IOReturn transferStage(DWC2Endpoint *ep, uint8_t *pid, bool isIn,
	                       void *buffer, uint32_t len, uint32_t *actualLen,
	                       uint32_t timeoutMS);

	// Current USB frame number, HFNUM.FRNUM (14 bits, wraps every 16384
	// frames). The UIM extends this to 64 bits itself.
	uint32_t frameNumber(void) const;

	// Raw HPRT0, for the UIM's root-hub GET_PORT_STATUS simulation. Returned
	// raw rather than pre-decoded because the caller has to translate DWC2's
	// bit layout into the USB hub-class layout, and doing that translation in
	// one place beside the hub constants is clearer than splitting it.
	uint32_t rootPortRegister(void) const;

	// SET_FEATURE / CLEAR_FEATURE on the root port, taking USB hub-class
	// feature selectors (kUSBHubPortPowerFeature and friends). Routes to the
	// same portPowerOn()/portReset()/portWriteHPRT0() helpers this driver
	// already uses, so the UIM cannot drive HPRT0 in a way that diverges from
	// the driver's own sequencing.
	IOReturn rootPortFeature(uint16_t feature, bool set);

	// --- Enumeration and hub protocol (BCM2837USBEnum.cpp) ---

	// Walk the bus: address and configure the root-port device, and if it
	// is a hub, power/reset its ports and enumerate what is behind them.
	// Returns the number of devices found.
	uint32_t enumerateBus(void);

	// Fill `ep` so it addresses `dev`'s endpoint `epAddr` (bit 7 = IN;
	// 0 = the default control endpoint). Returns false if that endpoint is
	// not one this device reported.
	bool     makeEndpoint(const DWC2USBDevice *dev, uint8_t epAddr, DWC2Endpoint *ep) const;

	const DWC2USBDevice *deviceAt(uint32_t index) const;
	uint32_t deviceCount(void) const { return fNumDevices; }

	// Find the first enumerated device whose interface class matches, e.g.
	// 0xFF (vendor-specific) for the LAN9514's Ethernet function.
	const DWC2USBDevice *findDeviceByInterfaceClass(uint8_t interfaceClass) const;

private:
	DWC2USBDevice fDevices[kDWC2MaxDevices];
	uint32_t      fNumDevices;
	uint8_t       fNextAddress;

	// Control transfers against the default (address 0 / unconfigured)
	// pipe of a device that has just been reset.
	bool enumerateDevice(uint8_t speed, uint8_t parentHubAddr, uint8_t parentPort,
	                     uint8_t splitHubAddr, uint8_t splitHubPort,
	                     DWC2USBDevice **outDev);
	bool parseConfiguration(DWC2USBDevice *dev, const uint8_t *cfg, uint32_t cfgLen);

	// Hub class requests (USB 2.0 spec chapter 11).
	bool hubGetDescriptor(DWC2USBDevice *hub, USBHubDescriptor *desc);
	bool hubSetPortFeature(DWC2USBDevice *hub, uint8_t port, uint16_t feature);
	bool hubClearPortFeature(DWC2USBDevice *hub, uint8_t port, uint16_t feature);
	bool hubGetPortStatus(DWC2USBDevice *hub, uint8_t port,
	                      uint16_t *status, uint16_t *change);
	bool hubEnumeratePorts(DWC2USBDevice *hub);
};

#endif /* _BCM2837_DWC2_H */
