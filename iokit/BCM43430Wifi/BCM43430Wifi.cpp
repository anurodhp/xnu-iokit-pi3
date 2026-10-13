/*
 * BCM43430Wifi - see BCM43430Wifi.h and docs/wifi-plan.md.
 *
 * Citations below name the function in Plan 9/Circle ether4330.c or the
 * file in Linux drivers/net/wireless/broadcom/brcm80211/brcmfmac that the
 * code follows.
 */
#include "BCM43430Wifi.h"
#include "BCM2837SDIODevice.h"

#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <sys/kpi_mbuf.h>
#include <stdarg.h>
#include <string.h>

#define super IOEthernetController
OSDefineMetaClassAndStructors(BCM43430Wifi, IOEthernetController);

extern "C" int vsnprintf(char *, size_t, const char *, va_list);

namespace {

enum {
	Fn0 = 0, Fn1 = 1, Fn2 = 2,

	// CCCR / FBR (SDIO spec 6.9-6.10)
	kCccrIoEnable  = 0x02,
	kCccrIoReady   = 0x03,
	kCccrIntEnable = 0x04,
	kCccrIntPend   = 0x05,
	kCccrIoAbort   = 0x06,
	kCccrBusIfc    = 0x07,
	kFbr1          = 0x100,
	kFbr2          = 0x200,
	kFbrBlkSize    = 0x10,

	// Function 1 backplane registers (ether4330 enum; brcmfmac sdio.h SBSDIO_*)
	kSbAddr     = 0x1000a,
	kWatermark  = 0x10008,
	kFrameCtl   = 0x1000d,
	kClkCsr     = 0x1000e,
	kPullups    = 0x1000f,
	kWfrmCnt    = 0x10019,
	kRfrmCnt    = 0x1001b,
	kRfHalt     = 0x01,
	kWfHalt     = 0x02,
	kForceALP   = 0x01,
	kForceHT    = 0x02,
	kReqALP     = 0x08,
	kReqHT      = 0x10,
	kNoHwReq    = 0x20,
	kALPAvail   = 0x40,
	kHTAvail    = 0x80,

	kSbWinSize  = 0x8000,
	kSb32Bit    = 0x8000,
	kEnumBase   = 0x18000000,

	// core wrapper registers
	kIoCtrl     = 0x408,
	kResetCtrl  = 0x800,

	// socram registers
	kCoreInfo   = 0x00,
	kBankIdx    = 0x10,
	kBankInfo   = 0x40,
	kBankPda    = 0x44,

	// chipcommon
	kGpioPullup   = 0x58,
	kGpioPulldown = 0x5c,

	// sdio core registers
	kIntStatus    = 0x20,
	kIntMask      = 0x24,
	kSbMbox       = 0x40,
	kSbMboxData   = 0x48,
	kHostMboxData = 0x4c,
	kFcChange     = 1 << 5,
	kFrameInt     = 1 << 6,
	kMailboxInt   = 1 << 7,

	kARMcm3   = 0x82A,
	kARM7tdmi = 0x825,
	kARMcr4   = 0x83E,

	// WLC ioctls (brcmfmac fwil.h BRCMF_C_*)
	WLC_UP           = 2,
	WLC_SET_PROMISC  = 10,
	WLC_SET_INFRA    = 20,
	WLC_SET_AUTH     = 22,
	WLC_SET_SSID     = 26,
	WLC_SET_PASSIVE_SCAN = 49,
	WLC_DISASSOC     = 52,
	WLC_SET_PM       = 86,
	WLC_GET_RSSI     = 127,
	WLC_SET_SCAN_CHANNEL_TIME = 185,
	WLC_SET_SCAN_UNASSOC_TIME = 187,
	WLC_GET_VAR      = 262,
	WLC_SET_VAR      = 263,
	WLC_SET_WSEC_PMK = 268,
	WLC_SET_SCAN_PASSIVE_TIME = 258,

	// events (brcmfmac fweh.h)
	E_SET_SSID      = 0,
	E_DEAUTH        = 5,
	E_DEAUTH_IND    = 6,
	E_DISASSOC      = 11,
	E_DISASSOC_IND  = 12,
	E_LINK          = 16,
	E_PSK_SUP       = 46,
	E_ESCAN_RESULT  = 69,
	kSupKeyed       = 6,     // BRCMF_E_STATUS_FWSUP_COMPLETED
	kSupTimeout     = 7,
	kStatusPartial  = 8,     // BRCMF_E_STATUS_PARTIAL (escan in progress)

	kSdpcmHdr = 12,
	kCdcHdr   = 16,
	kRxMax    = 2048,
};

inline void put2(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
inline void put4(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
inline uint32_t get2(const uint8_t *p) { return p[0] | p[1] << 8; }
inline uint32_t get4(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint32_t be2(const uint8_t *p) { return p[0] << 8 | p[1]; }
inline uint32_t be4(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

} // namespace

// ---------------------------------------------------------------- lifecycle

bool
BCM43430Wifi::init(OSDictionary *properties)
{
	if (!super::init(properties)) {
		return false;
	}
	fSDIO = NULL;
	fThread = NULL;
	fWake = fCardIrq = false;
	fNetif = NULL;
	fNetStats = NULL;
	fMedium = NULL;
	fNetifEnabled = false;
	fState = kWifiStateNone;
	fChipId = fChipRev = fArmCore = 0;
	fChipCommon = fArmCtl = fArmRegs = fD11Ctl = fSocramCtl = fSocramRegs = fSdRegs = 0;
	fSocramRev = fSdioRev = fSocramSize = fRamBase = 0;
	fWindow = ~0U;
	fFwBytes = 0;
	fNvram = fClm = NULL;
	fNvramLen = fClmLen = 0;
	bzero(fMAC, sizeof(fMAC));
	fFwVersion[0] = fLastError[0] = 0;
	fTxSeq = fTxWindow = fFcMask = 0;
	fCtlWaiting = false;
	fReqId = 0;
	fRespWant = 0;
	fRespReady = false;
	fRespStatus = 0;
	fRespLen = 0;
	fJoinResult = -1;
	fJoinWpa = fKeyed = fLinkUp = fAssociated = false;
	fPskStatus = -1;
	fSsid[0] = 0;
	bzero(fBssid, sizeof(fBssid));
	fScanning = fScanDone = false;
	fTxHead = fTxTail = fTxDrops = 0;
	fTxStalled = false;

	fBusLock = IOLockAlloc();
	fEventLock = IOLockAlloc();
	fCmdLock = IOLockAlloc();
	fOpLock = IOLockAlloc();
	fTxLock = IOSimpleLockAlloc();
	fRxBuf = (uint8_t *)IOMalloc(kRxMax);
	fCmdBuf = (uint8_t *)IOMalloc(kRxMax);
	fTxFrame = (uint8_t *)IOMalloc(kRxMax);
	fRespBuf = (uint8_t *)IOMalloc(kRxMax);
	fNvram = (uint8_t *)IOMalloc(8192);
	fClm = (uint8_t *)IOMalloc(65536);
	fScan = (wifi_scan_results *)IOMalloc(sizeof(wifi_scan_results));
	fTxRing = (TxSlot *)IOMalloc(sizeof(TxSlot) * kTxSlots);
	return fBusLock && fEventLock && fCmdLock && fOpLock && fTxLock && fRxBuf && fCmdBuf &&
	    fTxFrame && fRespBuf && fNvram && fClm && fScan && fTxRing;
}

void
BCM43430Wifi::free(void)
{
	// The driver is never unloaded (compiled into the kernel, no stop()
	// path tears the thread down); this only runs if init()/start() failed
	// before the thread existed.
	if (fBusLock) IOLockFree(fBusLock);
	if (fEventLock) IOLockFree(fEventLock);
	if (fCmdLock) IOLockFree(fCmdLock);
	if (fOpLock) IOLockFree(fOpLock);
	if (fTxLock) IOSimpleLockFree(fTxLock);
	if (fRxBuf) IOFree(fRxBuf, kRxMax);
	if (fCmdBuf) IOFree(fCmdBuf, kRxMax);
	if (fTxFrame) IOFree(fTxFrame, kRxMax);
	if (fRespBuf) IOFree(fRespBuf, kRxMax);
	if (fNvram) IOFree(fNvram, 8192);
	if (fClm) IOFree(fClm, 65536);
	if (fScan) IOFree(fScan, sizeof(wifi_scan_results));
	if (fTxRing) IOFree(fTxRing, sizeof(TxSlot) * kTxSlots);
	OSSafeReleaseNULL(fMedium);
	super::free();
}

bool
BCM43430Wifi::start(IOService *provider)
{
	fSDIO = OSDynamicCast(BCM2837SDIODevice, provider);
	if (fSDIO == NULL || !super::start(provider)) {
		return false;
	}
	fSDIO->setCardInterruptHandler(&BCM43430Wifi::cardInterrupt, this);

	// Chip probing (SDIO enumeration, backplane scan) blocks for up to a
	// few hundred ms and can fail outright when nothing answers (QEMU has
	// no WiFi chip), so it runs on the driver's own thread, never on the
	// IOKit matching thread.
	if (kernel_thread_start(&BCM43430Wifi::threadEntry, this, &fThread) != KERN_SUCCESS) {
		IOLog("BCM43430Wifi: could not start the SDIO thread\n");
		return false;
	}
	thread_deallocate(fThread);

	setProperty("IOUserClientClass", "BCM43430WifiUserClient");
	registerService();
	IOLog("BCM43430Wifi: started\n");
	return true;
}

void
BCM43430Wifi::setError(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(fLastError, sizeof(fLastError), fmt, ap);
	va_end(ap);
	IOLog("BCM43430Wifi: %s\n", fLastError);
}

// ------------------------------------------------------- SDIO primitives
// Callers hold fBusLock.

bool
BCM43430Wifi::cfgw(uint32_t off, uint8_t v)
{
	// ether4330 sdiowr(): retried, the chip occasionally NAKs a CMD52 while
	// its backplane clock is switching.
	for (int retry = 0; retry < 10; retry++) {
		if (fSDIO->writeDirect(Fn1, off, v) == kIOReturnSuccess) {
			return true;
		}
	}
	IOLog("BCM43430Wifi: CMD52 write fn1 %#x=%#x failed\n", off, v);
	return false;
}

bool
BCM43430Wifi::cfgr(uint32_t off, uint8_t *v)
{
	return fSDIO->readDirect(Fn1, off, v) == kIOReturnSuccess;
}

uint8_t
BCM43430Wifi::cfgr8(uint32_t off)
{
	uint8_t v = 0;
	cfgr(off, &v);
	return v;
}

bool
BCM43430Wifi::cfgreadl(uint32_t off, uint32_t *v)
{
	// 4-byte backplane read through the current window (ether4330
	// cfgreadl(); the window offset is off & 0x7fff, with the 32-bit access
	// flag 0x8000).
	uint8_t b[4] = {0};
	if (fSDIO->readExtended(Fn1, (off & (kSbWinSize - 1)) | kSb32Bit, true, b, 4) != kIOReturnSuccess) {
		return false;
	}
	*v = get4(b);
	return true;
}

bool
BCM43430Wifi::cfgwritel(uint32_t off, uint32_t v)
{
	uint8_t b[4];
	put4(b, v);
	for (int retry = 0; retry < 3; retry++) {
		if (fSDIO->writeExtended(Fn1, (off & (kSbWinSize - 1)) | kSb32Bit, true, b, 4) == kIOReturnSuccess) {
			return true;
		}
		fSDIO->writeDirect(Fn0, kCccrIoAbort, Fn1);   // ether4330 sdioabort(fn)
	}
	IOLog("BCM43430Wifi: backplane write %#x=%#x failed\n", off, v);
	return false;
}

bool
BCM43430Wifi::sbwindow(uint32_t addr)
{
	addr &= ~(kSbWinSize - 1);
	if (addr == fWindow) {
		return true;
	}
	bool ok = cfgw(kSbAddr, (addr >> 8) & 0x80) && cfgw(kSbAddr + 1, addr >> 16) &&
	    cfgw(kSbAddr + 2, addr >> 24);
	fWindow = ok ? addr : ~0U;
	return ok;
}

bool
BCM43430Wifi::sbmem(bool write, uint8_t *buf, uint32_t len, uint32_t off)
{
	// ether4330 sbmem()/sbrw(): per window, a multiple-of-4 CMD53 run then
	// single-byte CMD52s for any tail.
	uint32_t n = ((off + kSbWinSize) & ~(kSbWinSize - 1)) - off;
	while (len > 0) {
		if (n > len) {
			n = len;
		}
		if (!sbwindow(off)) {
			return false;
		}
		uint32_t woff = off & (kSbWinSize - 1);
		uint32_t m = n & ~3U;
		if (m > 0) {
			IOReturn r = write ?
			    fSDIO->writeExtended(Fn1, woff | kSb32Bit, true, buf, m) :
			    fSDIO->readExtended(Fn1, woff | kSb32Bit, true, buf, m);
			if (r != kIOReturnSuccess) {
				IOLog("BCM43430Wifi: sbmem %s %#x+%u failed\n", write ? "write" : "read", off, m);
				return false;
			}
		}
		for (uint32_t i = m; i < n; i++) {
			bool ok = write ? (fSDIO->writeDirect(Fn1, (woff + i) | kSb32Bit, buf[i]) == kIOReturnSuccess)
			                : cfgr((woff + i) | kSb32Bit, &buf[i]);
			if (!ok) {
				return false;
			}
		}
		off += n;
		buf += n;
		len -= n;
		n = kSbWinSize;
	}
	return true;
}

void
BCM43430Wifi::sbdisable(uint32_t regs, uint32_t pre, uint32_t ioctl)
{
	// ether4330 sbdisable()
	uint32_t v = 0;
	sbwindow(regs);
	cfgreadl(regs + kResetCtrl, &v);
	if (v & 1) {
		cfgwritel(regs + kIoCtrl, 3 | ioctl);
		cfgreadl(regs + kIoCtrl, &v);
		return;
	}
	cfgwritel(regs + kIoCtrl, 3 | pre);
	cfgreadl(regs + kIoCtrl, &v);
	cfgwritel(regs + kResetCtrl, 1);
	IODelay(10);
	for (int i = 0; i < 1000; i++) {
		if (cfgreadl(regs + kResetCtrl, &v) && (v & 1)) {
			break;
		}
		IODelay(10);
	}
	cfgwritel(regs + kIoCtrl, 3 | ioctl);
	cfgreadl(regs + kIoCtrl, &v);
}

void
BCM43430Wifi::sbreset(uint32_t regs, uint32_t pre, uint32_t ioctl)
{
	// ether4330 sbreset()
	uint32_t v = 0;
	sbdisable(regs, pre, ioctl);
	sbwindow(regs);
	for (int i = 0; i < 1000; i++) {
		if (!cfgreadl(regs + kResetCtrl, &v) || !(v & 1)) {
			break;
		}
		cfgwritel(regs + kResetCtrl, 0);
		IODelay(40);
	}
	cfgwritel(regs + kIoCtrl, 1 | ioctl);
	cfgreadl(regs + kIoCtrl, &v);
}

// ------------------------------------------------------------ chip bring-up

bool
BCM43430Wifi::sdioInit(void)
{
	// ether4330 sdioinit(), minus the pinmux (BCM2837SDHCI did that) and the
	// high-speed switch (we stay at 25 MHz default speed).
	uint32_t ocr = 0;
	fSDIO->command(0, 0, BCM2837SDIODevice::kRespNone, NULL);
	IOReturn r = fSDIO->command(5, 0, BCM2837SDIODevice::kRespR4, &ocr);
	for (int i = 0; !(r == kIOReturnSuccess && (ocr & 0x80000000)); i++) {
		if (i >= 5) {
			setError("no response to SDIO CMD5 (ocr %#x) - no WiFi chip on this bus?", ocr);
			return false;
		}
		r = fSDIO->command(5, 3U << 20 /* 3.2-3.4 V */, BCM2837SDIODevice::kRespR4, &ocr);
		if (!(r == kIOReturnSuccess && (ocr & 0x80000000))) {
			IOSleep(100);
		}
	}
	uint32_t resp = 0;
	if (fSDIO->command(3, 0, BCM2837SDIODevice::kRespR1, &resp) != kIOReturnSuccess) {
		setError("CMD3 failed");
		return false;
	}
	uint32_t rca = resp >> 16;
	if (fSDIO->command(7, rca << 16, BCM2837SDIODevice::kRespR1b, NULL) != kIOReturnSuccess) {
		setError("CMD7 failed");
		return false;
	}
	uint32_t hz = 0;
	fSDIO->setClock(25000000, &hz);

	uint8_t v = 0;
	if (fSDIO->readDirect(Fn0, kCccrBusIfc, &v) != kIOReturnSuccess ||
	    fSDIO->writeDirect(Fn0, kCccrBusIfc, (v & ~3) | 2) != kIOReturnSuccess) {
		setError("CCCR bus width write failed");
		return false;
	}
	fSDIO->setBusWidth4(true);

	fSDIO->writeDirect(Fn0, kFbr1 + kFbrBlkSize, 64);
	fSDIO->writeDirect(Fn0, kFbr1 + kFbrBlkSize + 1, 0);
	fSDIO->writeDirect(Fn0, kFbr2 + kFbrBlkSize, 512 & 0xff);
	fSDIO->writeDirect(Fn0, kFbr2 + kFbrBlkSize + 1, 512 >> 8);
	fSDIO->setFunctionBlockSize(1, 64);
	fSDIO->setFunctionBlockSize(2, 512);

	fSDIO->readDirect(Fn0, kCccrIoEnable, &v);
	fSDIO->writeDirect(Fn0, kCccrIoEnable, v | (1 << Fn1));
	fSDIO->writeDirect(Fn0, kCccrIntEnable, 0);
	for (int i = 0;; i++) {
		if (fSDIO->readDirect(Fn0, kCccrIoReady, &v) == kIOReturnSuccess && (v & (1 << Fn1))) {
			break;
		}
		if (i == 10) {
			setError("SDIO function 1 never became ready");
			return false;
		}
		IOSleep(100);
	}
	IOLog("BCM43430Wifi: SDIO card up, RCA %#x, OCR %#x, %u Hz 4-bit\n", rca, ocr, hz);
	return true;
}

bool
BCM43430Wifi::coreScan(uint32_t erom)
{
	// ether4330 corescan(): walk the EROM descriptor table.
	uint8_t *buf = (uint8_t *)IOMalloc(512);
	if (buf == NULL || !sbmem(false, buf, 512, erom)) {
		if (buf) IOFree(buf, 512);
		return false;
	}
	uint32_t coreid = 0, corerev = 0;
	for (int i = 0; i < 512; i += 4) {
		switch (buf[i] & 0xF) {
		case 0xF:
			i = 512;
			break;
		case 0x1:
			// Two-word descriptor: the second word must be inside the buffer.
			if (i + 8 > 512 || (buf[i + 4] & 0xF) != 0x1) {
				break;
			}
			coreid = (buf[i + 1] | buf[i + 2] << 8) & 0xFFF;
			i += 4;
			corerev = buf[i + 3];
			break;
		case 0x5: {
			uint32_t addr = (buf[i + 1] << 8 | buf[i + 2] << 16 | (uint32_t)buf[i + 3] << 24) & ~0xFFFU;
			bool wrap = (buf[i] & 0xC0) != 0;
			switch (coreid) {
			case 0x800:
				if (!wrap) fChipCommon = addr;
				break;
			case kARMcm3: case kARM7tdmi: case kARMcr4:
				fArmCore = coreid;
				if (wrap) { if (fArmCtl == 0) fArmCtl = addr; }
				else if (fArmRegs == 0) fArmRegs = addr;
				break;
			case 0x80E:
				if (wrap) fSocramCtl = addr;
				else if (fSocramRegs == 0) fSocramRegs = addr;
				fSocramRev = corerev;
				break;
			case 0x829:
				if (!wrap) fSdRegs = addr;
				fSdioRev = corerev;
				break;
			case 0x812:
				if (wrap) fD11Ctl = addr;
				break;
			}
			break;
		}
		}
	}
	IOFree(buf, 512);
	return true;
}

bool
BCM43430Wifi::ramScan(void)
{
	// ether4330 ramscan(), the SOCRAM (ARM CM3) branch: the 43430 has no CR4.
	if (fArmCore == kARMcr4) {
		setError("ARM CR4 chips are not supported by this driver");
		return false;
	}
	if (fSocramRev <= 7 || fSocramRev == 12) {
		setError("SOCRAM rev %u not supported", fSocramRev);
		return false;
	}
	sbreset(fSocramCtl, 0, 0);
	sbwindow(fSocramRegs);
	uint32_t n = 0;
	cfgreadl(fSocramRegs + kCoreInfo, &n);
	uint32_t banks = (n >> 4) & 0xF, size = 0;
	for (uint32_t i = 0; i < banks; i++) {
		cfgwritel(fSocramRegs + kBankIdx, i);
		cfgreadl(fSocramRegs + kBankInfo, &n);
		size += 8192 * ((n & 0x3F) + 1);
	}
	fSocramSize = size;
	fRamBase = 0;
	if (fChipId == 43430) {
		// ether4330: bank 3 power-down-array off (brcmfmac chip.c
		// brcmf_chip_socram_ramsize() does the same for 43430).
		cfgwritel(fSocramRegs + kBankIdx, 3);
		cfgwritel(fSocramRegs + kBankPda, 0);
	}
	return fSocramSize != 0;
}

bool
BCM43430Wifi::sbInit(void)
{
	// ether4330 sbinit()
	fWindow = ~0U;
	fChipCommon = fArmCtl = fArmRegs = fD11Ctl = fSocramCtl = fSocramRegs = fSdRegs = 0;
	uint32_t r = 0;
	if (!sbwindow(kEnumBase) || !cfgreadl(kEnumBase, &r)) {
		setError("cannot read the chip id register");
		return false;
	}
	fChipId = r & 0xFFFF;
	fChipRev = (r >> 16) & 0xF;
	IOLog("BCM43430Wifi: chip %u rev %u type %u\n", fChipId, fChipRev, (r >> 28) & 0xF);
	if (fChipId != 43430) {
		setError("chip id %u (%#x) is not the 43430 this driver supports", fChipId, fChipId);
		return false;
	}
	uint32_t erom = 0;
	if (!cfgreadl(kEnumBase + 63 * 4, &erom) || !coreScan(erom)) {
		setError("EROM core scan failed");
		return false;
	}
	if (fArmCtl == 0 || fD11Ctl == 0 || fSocramCtl == 0 || fSocramRegs == 0 || fSdRegs == 0) {
		setError("core scan missing cores (arm %#x d11 %#x socram %#x/%#x sdio %#x)",
		    fArmCtl, fD11Ctl, fSocramCtl, fSocramRegs, fSdRegs);
		return false;
	}
	sbdisable(fArmCtl, 0, 0);
	sbreset(fD11Ctl, 8 | 4, 4);
	if (!ramScan()) {
		return false;
	}
	IOLog("BCM43430Wifi: ARM %#x (core %#x) D11 %#x SOCRAM %#x/%#x rev %u SDIO %#x rev %u, RAM %u bytes\n",
	    fArmCtl, fArmCore, fD11Ctl, fSocramCtl, fSocramRegs, fSocramRev, fSdRegs, fSdioRev, fSocramSize);

	cfgw(kClkCsr, 0);
	IODelay(10);
	cfgw(kClkCsr, kNoHwReq | kReqALP);
	for (int i = 0; !(cfgr8(kClkCsr) & (kHTAvail | kALPAvail)); i++) {
		if (i > 10000) {
			setError("ALP clock never became available");
			return false;
		}
		IODelay(10);
	}
	cfgw(kClkCsr, kNoHwReq | kForceALP);
	IODelay(65);
	cfgw(kPullups, 0);
	sbwindow(fChipCommon);
	cfgwritel(fChipCommon + kGpioPullup, 0);
	cfgwritel(fChipCommon + kGpioPulldown, 0);
	return true;
}

bool
BCM43430Wifi::probeChip(void)
{
	IOLockLock(fBusLock);
	bool ok = sdioInit() && sbInit();
	IOLockUnlock(fBusLock);
	fState = ok ? kWifiStateProbed : kWifiStateFailed;
	if (ok) {
		IOLog("BCM43430Wifi: chip ready for firmware (run `wifi up`)\n");
	}
	return ok;
}

bool
BCM43430Wifi::sbEnable(void)
{
	// ether4330 sbenable(), plus brcmfmac's default F2 watermark
	// (sdio.c brcmf_sdio_bus_preinit(): SBSDIO_WATERMARK = 8). ForceHT is set
	// and left on, as ether4330 sbenable() does (Circle
	// addon/wlan/ether4330.c:967); that keeps the chip awake without
	// brcmfmac's KSO handling (wifi-plan.md review, S2).
	cfgw(kClkCsr, 0);
	IODelay(1000);
	cfgw(kClkCsr, kReqHT);
	for (int i = 0; !(cfgr8(kClkCsr) & kHTAvail); i++) {
		if (i == 50) {
			setError("HT clock never became available (CLKCSR %#x)", cfgr8(kClkCsr));
			return false;
		}
		IOSleep(100);
	}
	cfgw(kClkCsr, cfgr8(kClkCsr) | kForceHT);
	IODelay(10000);
	sbwindow(fSdRegs);
	cfgwritel(fSdRegs + kSbMboxData, 4 << 16);   // SDPCM protocol version 4
	cfgwritel(fSdRegs + kIntMask, kFrameInt | kMailboxInt | kFcChange);
	uint8_t v = 0;
	fSDIO->readDirect(Fn0, kCccrIoEnable, &v);
	fSDIO->writeDirect(Fn0, kCccrIoEnable, v | (1 << Fn2));
	for (int i = 0;; i++) {
		if (fSDIO->readDirect(Fn0, kCccrIoReady, &v) == kIOReturnSuccess && (v & (1 << Fn2))) {
			break;
		}
		if (i == 20) {
			setError("SDIO function 2 never became ready (IORDY %#x) - firmware did not start?", v);
			return false;
		}
		IOSleep(100);
	}
	fSDIO->writeDirect(Fn0, kCccrIntEnable, (1 << Fn1) | (1 << Fn2) | 1);
	cfgw(kWatermark, 8);
	return true;
}

// ---------------------------------------------------------- firmware upload

IOReturn
BCM43430Wifi::uploadBegin(void)
{
	if (fState == kWifiStateUp) {
		setError("firmware already running; reboot to load it again");
		return kIOReturnBusy;
	}
	if (fState == kWifiStateNone) {
		return kIOReturnNotReady;
	}
	IOLockLock(fBusLock);
	// Re-run the backplane init: halts the ARM and resets D11/SOCRAM, so a
	// half-finished earlier upload leaves nothing behind (review N3).
	bool ok = sbInit();
	if (ok) {
		cfgw(kClkCsr, kReqALP);
		for (int i = 0; !(cfgr8(kClkCsr) & kALPAvail); i++) {
			if (i > 10000) { ok = false; break; }
			IODelay(10);
		}
		uint8_t zero[4] = {0, 0, 0, 0};
		ok = ok && sbmem(true, zero, 4, fRamBase + fSocramSize - 4);
	}
	IOLockUnlock(fBusLock);
	fFwBytes = fNvramLen = fClmLen = 0;
	fState = ok ? kWifiStateLoading : kWifiStateFailed;
	return ok ? kIOReturnSuccess : kIOReturnIOError;
}

IOReturn
BCM43430Wifi::uploadChunk(uint32_t type, uint32_t offset, const void *data, uint32_t len)
{
	if (fState != kWifiStateLoading) {
		return kIOReturnNotReady;
	}
	if (len == 0 || len > kWifiChunkMax) {
		return kIOReturnBadArgument;
	}
	switch (type) {
	case kWifiBlobFirmware: {
		if (offset + len > fSocramSize || offset + len < offset) {
			return kIOReturnBadArgument;
		}
		// Pad to 4 and verify by reading back (ether4330 Firmwarecmp).
		uint32_t padded = (len + 3) & ~3U;
		uint8_t *buf = (uint8_t *)IOMalloc(kWifiChunkMax);
		uint8_t *chk = (uint8_t *)IOMalloc(kWifiChunkMax);
		if (buf == NULL || chk == NULL) {
			if (buf) IOFree(buf, kWifiChunkMax);
			if (chk) IOFree(chk, kWifiChunkMax);
			return kIOReturnNoMemory;
		}
		bzero(buf, padded);
		memcpy(buf, data, len);
		IOLockLock(fBusLock);
		bool ok = sbmem(true, buf, padded, fRamBase + offset) &&
		    sbmem(false, chk, padded, fRamBase + offset);
		IOLockUnlock(fBusLock);
		if (ok && memcmp(buf, chk, padded) != 0) {
			setError("firmware verify mismatch at offset %u", offset);
			ok = false;
		}
		IOFree(buf, kWifiChunkMax);
		IOFree(chk, kWifiChunkMax);
		if (!ok) {
			fState = kWifiStateFailed;
			return kIOReturnIOError;
		}
		if (offset + len > fFwBytes) {
			fFwBytes = offset + len;
		}
		return kIOReturnSuccess;
	}
	case kWifiBlobNvram:
		if (offset != fNvramLen || offset + len > 8192 - 8) {
			return kIOReturnBadArgument;
		}
		memcpy(fNvram + offset, data, len);
		fNvramLen += len;
		return kIOReturnSuccess;
	case kWifiBlobClm:
		if (offset != fClmLen || offset + len > 65536) {
			return kIOReturnBadArgument;
		}
		memcpy(fClm + offset, data, len);
		fClmLen += len;
		return kIOReturnSuccess;
	}
	return kIOReturnBadArgument;
}

static uint32_t
condenseNvram(uint8_t *buf, uint32_t n)
{
	// ether4330 condense(): strip comments, blank lines and CRs, NUL-separate
	// lines, end with a double NUL, pad to a multiple of 4.
	uint8_t *op = buf, *lp = buf, *ep = buf + n;
	bool skipping = false;
	for (uint8_t *p = buf; p < ep; p++) {
		uint8_t c = *p;
		if (c == '#') {
			skipping = true;
		} else if (c == '\0' || c == '\n') {
			skipping = false;
			if (op != lp) {
				*op++ = '\0';
				lp = op;
			}
		} else if (c != '\r' && !skipping) {
			*op++ = c;
		}
	}
	if (!skipping && op != lp) {
		*op++ = '\0';
	}
	*op++ = '\0';
	while ((op - buf) & 3) {
		*op++ = '\0';
	}
	return (uint32_t)(op - buf);
}

IOReturn
BCM43430Wifi::boot(void)
{
	if (fState != kWifiStateLoading || fFwBytes == 0 || fNvramLen == 0) {
		setError("boot: firmware/NVRAM not uploaded (fw %u, nvram %u bytes)", fFwBytes, fNvramLen);
		return kIOReturnNotReady;
	}
	uint32_t n = condenseNvram(fNvram, fNvramLen);
	IOLockLock(fBusLock);
	// ether4330 fwload(): NVRAM at the top of RAM, then the length token in
	// the last word: (words & 0xffff) | (~words << 16).
	bool ok = sbmem(true, fNvram, n, fRamBase + fSocramSize - n - 4);
	uint8_t tok[4];
	uint32_t words = n / 4;
	put4(tok, (words & 0xFFFF) | (~words << 16));
	ok = ok && sbmem(true, tok, 4, fRamBase + fSocramSize - 4);
	if (ok) {
		sbreset(fArmCtl, 0, 0);       // CM3: release the ARM, firmware starts
		ok = sbEnable();
	}
	if (ok) {
		// brcmfmac brcmf_sdio_ulp_reinit_fw() (sdio.c:2638-2643: "on FMAC side
		// tx_max is initially set to 4"): tx_seq 0, tx_max 4, until the
		// first frame header from the firmware reports the real window.
		fTxSeq = 0;
		fTxWindow = 4;
		fFcMask = 0;
		fState = kWifiStateUp;
	} else {
		fState = kWifiStateFailed;
	}
	IOLockUnlock(fBusLock);
	if (!ok) {
		return kIOReturnIOError;
	}
	IOLog("BCM43430Wifi: firmware started (%u bytes, NVRAM %u bytes)\n", fFwBytes, n);
	fSDIO->enableCardInterrupt(true);
	wakeThread();

	IOReturn r = clmLoad();
	if (r == kIOReturnSuccess) {
		r = wlInit();
	}
	if (r != kIOReturnSuccess) {
		fState = kWifiStateFailed;
		return r;
	}

	if (fNetif == NULL) {
		if (!createMedium() || !attachInterface((IONetworkInterface **)&fNetif, true)) {
			setError("attachInterface failed");
			return kIOReturnError;
		}
		IOLog("BCM43430Wifi: network interface attached\n");
	}
	updateLink();
	return kIOReturnSuccess;
}

// ----------------------------------------------------------------- frames

bool
BCM43430Wifi::writeFrame(uint8_t *frame, uint32_t len)
{
	// Caller holds fBusLock. The buffer has room to round up to 4.
	frame[4] = fTxSeq;
	uint32_t xfer = (len + 3) & ~3U;
	while (len < xfer) {
		frame[len++] = 0;
	}
	if (fSDIO->writeExtended(Fn2, 0, false, frame, xfer) != kIOReturnSuccess) {
		// ether4330 txstart(): halt the write frame and drain the counter.
		fSDIO->writeDirect(Fn0, kCccrIoAbort, Fn2);
		cfgw(kFrameCtl, kWfHalt);
		for (int i = 0; i < 1000 && (cfgr8(kWfrmCnt + 1) || cfgr8(kWfrmCnt)); i++) {
			IODelay(10);
		}
		return false;
	}
	fTxSeq++;
	return true;
}

int
BCM43430Wifi::readFrame(void)
{
	// ether4330 wlreadpkt(): 12-byte SDPCM header first, then the rest.
	// Caller holds fBusLock.
	if (fSDIO->readExtended(Fn2, 0, false, fRxBuf, kSdpcmHdr) != kIOReturnSuccess) {
		fSDIO->writeDirect(Fn0, kCccrIoAbort, Fn2);
		return -1;
	}
	uint32_t len = get2(fRxBuf), lenck = get2(fRxBuf + 2);
	if (len == 0) {
		return 0;
	}
	if ((len ^ 0xFFFF) != lenck || len < kSdpcmHdr || len > kRxMax) {
		IOLog("BCM43430Wifi: bad frame len %#x lenck %#x\n", len, lenck);
		cfgw(kFrameCtl, kRfHalt);
		for (int i = 0; i < 1000 && (cfgr8(kRfrmCnt + 1) || cfgr8(kRfrmCnt)); i++) {
			IODelay(10);
		}
		return -1;
	}
	if (len > kSdpcmHdr) {
		uint32_t rest = (len - kSdpcmHdr + 3) & ~3U;
		if (fSDIO->readExtended(Fn2, 0, false, fRxBuf + kSdpcmHdr, rest) != kIOReturnSuccess) {
			fSDIO->writeDirect(Fn0, kCccrIoAbort, Fn2);
			return -1;
		}
	}
	// Credit window and flow control ride in every frame (rproc()).
	fFcMask = fRxBuf[8];
	fTxWindow = fRxBuf[9];
	return (int)len;
}

void
BCM43430Wifi::serviceInterrupts(void)
{
	// ether4330 intwait(): read and ack the SDIO core's interrupt status;
	// answer the mailbox. Caller holds fBusLock.
	uint32_t ints = 0;
	if (!sbwindow(fSdRegs) || !cfgreadl(fSdRegs + kIntStatus, &ints) || ints == 0) {
		return;
	}
	cfgwritel(fSdRegs + kIntStatus, ints);
	if (ints & kMailboxInt) {
		uint32_t mbox = 0;
		cfgreadl(fSdRegs + kHostMboxData, &mbox);
		cfgwritel(fSdRegs + kSbMbox, 2);    // HOSTMBOX ack
		if (mbox & 0x8) {
			IOLog("BCM43430Wifi: firmware ready\n");
		}
	}
}

void
BCM43430Wifi::dispatchFrame(uint32_t len)
{
	uint8_t *p = fRxBuf;
	uint32_t doff = p[7];
	switch (p[5] & 0xF) {
	case 0: {   // control response: CDC header after the SDPCM header
		if (len < doff + kCdcHdr) {
			return;
		}
		const uint8_t *q = p + doff;
		uint16_t id = (uint16_t)get2(q + 10);
		IOLockLock(fEventLock);
		if (fRespWant != 0 && id == fRespWant) {
			fRespStatus = get4(q + 12);
			fRespLen = len - doff - kCdcHdr;
			memcpy(fRespBuf, q + kCdcHdr, fRespLen);
			fRespReady = true;
			fRespWant = 0;
			IOLockWakeup(fEventLock, &fRespReady, false);
		}
		IOLockUnlock(fEventLock);
		return;
	}
	case 1:     // event
	case 2: {   // data
		if (len <= doff + 4) {
			return;
		}
		uint32_t bdc = 4 + ((uint32_t)p[doff + 3] << 2);   // BDC header + its data offset
		if (len <= doff + bdc) {
			return;
		}
		const uint8_t *frame = p + doff + bdc;
		uint32_t flen = len - doff - bdc;
		if ((p[5] & 0xF) == 1) {
			handleEvent(frame, flen);
			return;
		}
		if (flen < 14 || fNetif == NULL || !fNetifEnabled) {
			return;
		}
		mbuf_t m = allocatePacket(flen);
		if (m == NULL) {
			return;
		}
		mbuf_copyback(m, 0, flen, frame, MBUF_DONTWAIT);
		fNetif->inputPacket(m, flen);
		return;
	}
	default:
		IOLog("BCM43430Wifi: frame on unknown channel %u\n", p[5] & 0xF);
	}
}

void
BCM43430Wifi::handleEvent(const uint8_t *p, uint32_t len)
{
	// Ethernet header (14) + bcmeth header (10) + brcmf_event_msg_be (48),
	// all big-endian (fweh.h; ether4330 bcmevent()).
	if (len < 14 + 10 + 48) {
		return;
	}
	const uint8_t *e = p + 24;
	uint32_t flags = be2(e + 2);
	uint32_t type = be4(e + 4);
	uint32_t status = be4(e + 8);
	uint32_t reason = be4(e + 12);
	uint32_t datalen = be4(e + 20);
	const uint8_t *data = e + 48;
	if (datalen > len - 72) {
		datalen = len - 72;
	}

	IOLockLock(fEventLock);
	switch (type) {
	case E_SET_SSID:
		memcpy(fBssid, e + 24, 6);
		fJoinResult = (int)status;
		if (status != 0) {
			fAssociated = false;
		}
		IOLockWakeup(fEventLock, &fJoinResult, false);
		break;
	case E_LINK:
		fLinkUp = (flags & 1) != 0;
		if (!fLinkUp) {
			fKeyed = false;
		}
		IOLockWakeup(fEventLock, &fJoinResult, false);
		break;
	case E_PSK_SUP:
		fPskStatus = (int)status;
		if (status == kSupKeyed) {
			fKeyed = true;
		}
		IOLockWakeup(fEventLock, &fJoinResult, false);
		break;
	case E_DEAUTH: case E_DEAUTH_IND: case E_DISASSOC: case E_DISASSOC_IND:
		fLinkUp = false;
		fKeyed = false;
		IOLockWakeup(fEventLock, &fJoinResult, false);
		break;
	case E_ESCAN_RESULT:
		IOLockUnlock(fEventLock);
		handleScanResult(data, datalen, status);
		IOLockLock(fEventLock);
		break;
	default:
		break;
	}
	bool assoc = fLinkUp && (!fJoinWpa || fKeyed);
	bool changed = (assoc != fAssociated);
	fAssociated = assoc;
	IOLockUnlock(fEventLock);

	if (type == E_SET_SSID || type == E_LINK || type == E_PSK_SUP || type == E_DEAUTH ||
	    type == E_DEAUTH_IND || type == E_DISASSOC || type == E_DISASSOC_IND) {
		IOLog("BCM43430Wifi: event %u status %u reason %u flags %#x\n", type, status, reason, flags);
	}
	if (changed) {
		updateLink();
	}
}

void
BCM43430Wifi::handleScanResult(const uint8_t *p, uint32_t len, uint32_t status)
{
	IOLockLock(fEventLock);
	if (!fScanning) {
		IOLockUnlock(fEventLock);
		return;
	}
	if (status != kStatusPartial) {
		fScanDone = true;
		IOLockWakeup(fEventLock, &fScanDone, false);
		IOLockUnlock(fEventLock);
		return;
	}
	// brcmf_escan_result_le: buflen, version, sync_id, bss_count, then
	// brcmf_bss_info_le records (offsets as ether4330 addscan()).
	if (len < 12) {
		IOLockUnlock(fEventLock);
		return;
	}
	uint32_t nbss = get2(p + 10);
	const uint8_t *b = p + 12;
	const uint8_t *end = p + len;
	for (uint32_t k = 0; k < nbss && b + 0x7c <= end; k++) {
		uint32_t blen = get4(b + 4);
		if (blen < 0x7c || b + blen > end) {
			break;
		}
		const uint8_t *bssid = b + 8;
		uint32_t i;
		for (i = 0; i < fScan->count; i++) {
			if (memcmp(fScan->entries[i].bssid, bssid, 6) == 0) {
				break;
			}
		}
		if (i == fScan->count && fScan->count < kWifiMaxScan) {
			wifi_scan_entry *s = &fScan->entries[fScan->count++];
			bzero(s, sizeof(*s));
			memcpy(s->bssid, bssid, 6);
			uint32_t sl = b[18] > 32 ? 32 : b[18];
			memcpy(s->ssid, b + 19, sl);
			s->channel = get2(b + 72) & 0xFF;
			s->rssi = (int16_t)get2(b + 78);
			uint32_t cap = get2(b + 16);
			if (cap & 0x10) {
				s->security |= kWifiSecPrivacy;
			}
			uint32_t ieoff = get4(b + 0x74), ielen = get4(b + 0x78);
			// Both values come from the chip; compare without a 32-bit sum
			// that can wrap.
			if (ieoff >= 0x7c && ieoff <= blen && ielen <= blen - ieoff) {
				const uint8_t *t = b + ieoff, *te = t + ielen;
				static const uint8_t wpaoui[4] = {0x00, 0x50, 0xf2, 0x01};
				while (t + 2 <= te && t + 2 + t[1] <= te) {
					if (t[0] == 0x30) {
						s->security |= kWifiSecWPA2;
					} else if (t[0] == 0xdd && t[1] >= 4 && memcmp(t + 2, wpaoui, 4) == 0) {
						s->security |= kWifiSecWPA;
					}
					t += 2 + t[1];
				}
			}
		}
		b += blen;
	}
	IOLockUnlock(fEventLock);
}

void
BCM43430Wifi::transmitPending(void)
{
	for (;;) {
		// Credit check under fBusLock, where fTxSeq (writeFrame) and
		// fTxWindow (readFrame) change, so a control frame cannot take
		// the last credit between check and write. brcmfmac data_ok()
		// (sdio.c): tx_max - tx_seq, less TXCTL_CREDITS (2) while a
		// control frame waits, must be nonzero and below 0x80.
		IOLockLock(fBusLock);
		uint8_t credit = (uint8_t)(fTxWindow - fTxSeq - (fCtlWaiting ? 2 : 0));
		if (credit == 0 || (credit & 0x80) || (fFcMask & (1 << 2))) {
			IOLockUnlock(fBusLock);
			return;
		}
		IOInterruptState st = IOSimpleLockLockDisableInterrupt(fTxLock);
		if (fTxHead == fTxTail) {
			IOSimpleLockUnlockEnableInterrupt(fTxLock, st);
			IOLockUnlock(fBusLock);
			return;
		}
		TxSlot *slot = &fTxRing[fTxTail % kTxSlots];
		uint32_t flen = slot->len;
		// SDPCM header (12) + BDC header (4: flags 0x20 = BDC version 2,
		// priority 0, flags2 0, data offset 0) + the Ethernet frame.
		uint32_t len = kSdpcmHdr + 4 + flen;
		bzero(fTxFrame, kSdpcmHdr + 4);
		memcpy(fTxFrame + kSdpcmHdr + 4, slot->data, flen);
		fTxTail++;
		IOSimpleLockUnlockEnableInterrupt(fTxLock, st);

		put2(fTxFrame, len);
		put2(fTxFrame + 2, ~len & 0xFFFF);
		fTxFrame[5] = 2;            // channel 2: data
		fTxFrame[7] = kSdpcmHdr;    // data offset
		fTxFrame[kSdpcmHdr] = 0x20;
		bool ok = writeFrame(fTxFrame, len);
		IOLockUnlock(fBusLock);
		if (!ok) {
			fTxDrops++;
		}
		if (fNetStats != NULL) {
			if (ok) {
				fNetStats->outputPackets++;
			} else {
				fNetStats->outputErrors++;
			}
		}
	}
}

void
BCM43430Wifi::cardInterrupt(OSObject *target)
{
	// Primary interrupt level: only flag and wake.
	BCM43430Wifi *self = (BCM43430Wifi *)target;
	self->fCardIrq = true;
	IOLockWakeup(self->fEventLock, (void *)&self->fWake, false);
}

void
BCM43430Wifi::wakeThread(void)
{
	IOLockLock(fEventLock);
	fWake = true;
	IOLockWakeup(fEventLock, (void *)&fWake, false);
	IOLockUnlock(fEventLock);
}

void
BCM43430Wifi::threadEntry(void *arg, wait_result_t)
{
	((BCM43430Wifi *)arg)->threadMain();
}

void
BCM43430Wifi::threadMain(void)
{
	probeChip();

	uint32_t idleTicks = 0;
	for (;;) {
		if (fState == kWifiStateUp) {
			bool pending = fCardIrq;
			fCardIrq = false;
			IOLockLock(fBusLock);
			uint8_t intpend = 0;
			fSDIO->readDirect(Fn0, kCccrIntPend, &intpend);
			bool drain = pending || intpend != 0 || (++idleTicks % 10) == 0;
			if (drain) {
				serviceInterrupts();
			}
			IOLockUnlock(fBusLock);
			if (drain) {
				for (int n = 0; n < 64; n++) {
					IOLockLock(fBusLock);
					int r = readFrame();
					IOLockUnlock(fBusLock);
					if (r <= 0) {
						break;
					}
					dispatchFrame((uint32_t)r);
				}
			}
			transmitPending();
			if (fTxStalled && (fTxHead - fTxTail) < kTxSlots) {
				fTxStalled = false;
				IOOutputQueue *q = getOutputQueue();
				if (q != NULL) {
					q->service(IOBasicOutputQueue::kServiceAsync);
				}
			}
			fSDIO->enableCardInterrupt(true);
		}

		IOLockLock(fEventLock);
		if (!fWake && !fCardIrq) {
			uint64_t deadline;
			clock_interval_to_deadline(fState == kWifiStateUp ? 10 : 200, kMillisecondScale, &deadline);
			IOLockSleepDeadline(fEventLock, (void *)&fWake, deadline, THREAD_UNINT);
		}
		fWake = false;
		IOLockUnlock(fEventLock);
	}
}

// ---------------------------------------------------------------- control

IOReturn
BCM43430Wifi::wlcmd(uint32_t cmd, bool set, const void *data, uint32_t dlen, void *out, uint32_t olen)
{
	// ether4330 wlcmd() / brcmfmac bcdc.c: SDPCM header, CDC header
	// {cmd, len, flags (bit 1 = set), id, status}, payload. Never called
	// from threadMain (it waits for threadMain to deliver the response).
	if (fState != kWifiStateUp) {
		return kIOReturnNotReady;
	}
	uint32_t tlen = set ? dlen : (dlen > olen ? dlen : olen);
	uint32_t len = kSdpcmHdr + kCdcHdr + tlen;
	if (len + 4 > kRxMax) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fCmdLock);
	bzero(fCmdBuf, len);
	put2(fCmdBuf, len);
	put2(fCmdBuf + 2, ~len & 0xFFFF);
	fCmdBuf[7] = kSdpcmHdr;
	uint8_t *q = fCmdBuf + kSdpcmHdr;
	put4(q, cmd);
	put4(q + 4, tlen);
	put2(q + 8, set ? 2 : 0);
	IOLockLock(fEventLock);
	uint16_t id = ++fReqId;
	if (id == 0) {
		id = ++fReqId;
	}
	fRespWant = id;
	fRespReady = false;
	IOLockUnlock(fEventLock);
	put2(q + 10, id);
	if (dlen > 0) {
		memcpy(q + kCdcHdr, data, dlen);
	}

	// brcmfmac sends a control frame only when txctl_ok() (sdio.c): a
	// credit is free. Wait for one without holding fBusLock, which
	// threadMain needs to read the frames that reopen the window;
	// fCtlWaiting makes transmitPending() leave 2 credits for us.
	bool ok = false;
	for (int tries = 0; ; tries++) {
		IOLockLock(fBusLock);
		uint8_t credit = (uint8_t)(fTxWindow - fTxSeq);
		if (credit != 0 && !(credit & 0x80)) {
			ok = writeFrame(fCmdBuf, len);
			IOLockUnlock(fBusLock);
			break;
		}
		IOLockUnlock(fBusLock);
		if (tries >= 2000) {
			IOLog("BCM43430Wifi: ioctl %u: no SDPCM credit for 2 s\n", cmd);
			break;
		}
		fCtlWaiting = true;
		wakeThread();
		IOSleep(1);
	}
	fCtlWaiting = false;
	wakeThread();
	bzero(fCmdBuf, len);    // may hold the PMK; the next command only clears its own length

	IOReturn ret = kIOReturnSuccess;
	IOLockLock(fEventLock);
	if (!ok) {
		ret = kIOReturnIOError;
	} else {
		uint64_t deadline;
		clock_interval_to_deadline(2000, kMillisecondScale, &deadline);
		while (!fRespReady) {
			if (IOLockSleepDeadline(fEventLock, &fRespReady, deadline, THREAD_UNINT) == THREAD_TIMED_OUT) {
				break;
			}
		}
		if (!fRespReady) {
			ret = kIOReturnTimeout;
		} else if (fRespStatus != 0) {
			ret = kIOReturnError;
		} else if (!set && out != NULL) {
			memcpy(out, fRespBuf, fRespLen < olen ? fRespLen : olen);
		}
	}
	fRespWant = 0;
	uint32_t status = fRespStatus;
	IOLockUnlock(fEventLock);
	IOLockUnlock(fCmdLock);

	if (ret == kIOReturnTimeout) {
		IOLog("BCM43430Wifi: ioctl %u timed out\n", cmd);
	} else if (ret == kIOReturnError) {
		const char *var = (cmd == WLC_GET_VAR || cmd == WLC_SET_VAR) ? (const char *)data : "";
		IOLog("BCM43430Wifi: ioctl %u %s failed, firmware status %d\n", cmd, var, (int)status);
	}
	return ret;
}

IOReturn
BCM43430Wifi::wlcmdint(uint32_t cmd, uint32_t val)
{
	uint8_t b[4];
	put4(b, val);
	return wlcmd(cmd, true, b, 4, NULL, 0);
}

IOReturn
BCM43430Wifi::wlgetint(uint32_t cmd, uint32_t *val)
{
	uint8_t b[4] = {0};
	IOReturn r = wlcmd(cmd, false, b, 4, b, 4);
	*val = get4(b);
	return r;
}

IOReturn
BCM43430Wifi::wlsetvar(const char *name, const void *val, uint32_t len)
{
	uint32_t nl = (uint32_t)strlen(name) + 1;
	if (nl + len > kRxMax - 64) {
		return kIOReturnBadArgument;
	}
	uint8_t *b = (uint8_t *)IOMalloc(nl + len);
	if (b == NULL) {
		return kIOReturnNoMemory;
	}
	memcpy(b, name, nl);
	memcpy(b + nl, val, len);
	IOReturn r = wlcmd(WLC_SET_VAR, true, b, nl + len, NULL, 0);
	IOFree(b, nl + len);
	return r;
}

IOReturn
BCM43430Wifi::wlgetvar(const char *name, void *out, uint32_t len)
{
	return wlcmd(WLC_GET_VAR, false, name, (uint32_t)strlen(name) + 1, out, len);
}

IOReturn
BCM43430Wifi::wlsetint(const char *name, uint32_t val)
{
	uint8_t b[4];
	put4(b, val);
	return wlsetvar(name, b, 4);
}

IOReturn
BCM43430Wifi::clmLoad(void)
{
	// brcmfmac common.c brcmf_c_download_clm() / ether4330 reguload():
	// struct brcmf_dload_data_le {le16 flag; le16 dload_type; le32 len;
	// le32 crc; u8 data[]} in <= 1400-byte chunks; flag = DLOAD_HANDLER_VER
	// (1) << 12 | DL_BEGIN (2) on the first | DL_END (4) on the last;
	// dload_type 2 = CLM. Mandatory: this firmware is built "noclminc".
	if (fClmLen == 0) {
		setError("no CLM blob uploaded; the firmware cannot transmit without one");
		return kIOReturnNotReady;
	}
	const uint32_t kChunk = 1400;
	uint8_t *buf = (uint8_t *)IOMalloc(12 + kChunk + 8);
	if (buf == NULL) {
		return kIOReturnNoMemory;
	}
	IOReturn r = kIOReturnSuccess;
	uint32_t off = 0;
	uint32_t flag = (1 << 12) | 2;
	while (off < fClmLen && r == kIOReturnSuccess) {
		uint32_t n = fClmLen - off;
		if (n > kChunk) {
			n = kChunk;
		} else {
			flag |= 4;
		}
		bzero(buf, 12 + kChunk + 8);
		put2(buf, flag);
		put2(buf + 2, 2);
		put4(buf + 4, n);
		put4(buf + 8, 0);
		memcpy(buf + 12, fClm + off, n);
		r = wlsetvar("clmload", buf, 12 + n);
		off += n;
		flag &= ~2U;
	}
	IOFree(buf, 12 + kChunk + 8);
	uint32_t st = ~0U;
	uint8_t sb[4] = {0};
	if (wlgetvar("clmload_status", sb, 4) == kIOReturnSuccess) {
		st = get4(sb);
	}
	if (r != kIOReturnSuccess || st != 0) {
		setError("CLM load failed (ioctl %#x, clmload_status %d)", r, (int)st);
		return kIOReturnError;
	}
	IOLog("BCM43430Wifi: CLM loaded (%u bytes)\n", fClmLen);
	return kIOReturnSuccess;
}

IOReturn
BCM43430Wifi::wlInit(void)
{
	// ether4330 wlinit(), with the review's additions (wifi-plan.md S2/S3).
	IOReturn r = wlgetvar("cur_etheraddr", fMAC, 6);
	if (r != kIOReturnSuccess) {
		setError("cannot read the MAC address (firmware not answering)");
		return r;
	}
	IOLog("BCM43430Wifi: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	    fMAC[0], fMAC[1], fMAC[2], fMAC[3], fMAC[4], fMAC[5]);
	wlsetint("assoc_listen", 10);
	wlcmdint(WLC_SET_PM, 0);                // power save off (43430, see header)
	wlsetint("bus:txglom", 0);
	wlsetint("bcn_timeout", 10);
	wlsetint("assoc_retry_max", 3);

	uint8_t mask[16];
	memset(mask, 0xFF, sizeof(mask));
#define DISABLE(n) mask[(n) / 8] &= ~(1 << ((n) % 8))
	DISABLE(40);    // E_RADIO
	DISABLE(44);    // E_PROBREQ_MSG
	DISABLE(54);    // E_IF
	DISABLE(71);    // E_PROBRESP_MSG
	DISABLE(20);    // E_TXFAIL
	DISABLE(124);
#undef DISABLE
	wlsetvar("event_msgs", mask, sizeof(mask));
	wlcmdint(WLC_SET_SCAN_CHANNEL_TIME, 0x28);
	wlcmdint(WLC_SET_SCAN_UNASSOC_TIME, 0x28);
	wlcmdint(WLC_SET_SCAN_PASSIVE_TIME, 0x82);
	wlcmdint(WLC_UP, 0);

	bzero(fFwVersion, sizeof(fFwVersion));
	if (wlgetvar("ver", fFwVersion, sizeof(fFwVersion) - 1) == kIOReturnSuccess) {
		char *nl = strchr(fFwVersion, '\n');
		if (nl) *nl = 0;
		IOLog("BCM43430Wifi: firmware %s\n", fFwVersion);
	}
	char cap[256];
	bzero(cap, sizeof(cap));
	if (wlgetvar("cap", cap, sizeof(cap) - 1) == kIOReturnSuccess) {
		IOLog("BCM43430Wifi: cap %s\n", cap);
	}
	wlsetint("roam_off", 1);
	wlcmdint(WLC_SET_INFRA, 1);
	wlcmdint(WLC_SET_PROMISC, 0);
	return wlcmdint(WLC_UP, 1);
}

// ------------------------------------------------------------- scan / join

IOReturn
BCM43430Wifi::scan(wifi_scan_results *out)
{
	if (fState != kWifiStateUp) {
		return kIOReturnNotReady;
	}
	IOLockLock(fEventLock);
	fScan->count = 0;
	fScanning = true;
	fScanDone = false;
	IOLockUnlock(fEventLock);

	// ether4330 wlscanstart(): escan v1, wildcard SSID, channels 1-14.
	static const uint8_t chanspecs[14 * 2] = {
		0x01,0x2b,0x02,0x2b,0x03,0x2b,0x04,0x2b,0x05,0x2e,0x06,0x2e,0x07,0x2e,
		0x08,0x2b,0x09,0x2b,0x0a,0x2b,0x0b,0x2b,0x0c,0x2b,0x0d,0x2b,0x0e,0x2b,
	};
	uint8_t params[4 + 2 + 2 + 4 + 32 + 6 + 1 + 1 + 4 * 4 + 2 + 2 + 14 * 2 + (4 + 32)];
	bzero(params, sizeof(params));
	uint8_t *p = params;
	put4(p, 1); p += 4;            // version
	put2(p, 1); p += 2;            // action start
	put2(p, 0x1234); p += 2;       // sync id
	p += 4 + 32;                   // ssid (wildcard)
	memset(p, 0xff, 6); p += 6;    // bssid broadcast
	*p++ = 2;                      // bss type any
	*p++ = 0;                      // active scan
	put4(p, ~0U); p += 4;          // nprobes
	put4(p, ~0U); p += 4;          // active time
	put4(p, ~0U); p += 4;          // passive time
	put4(p, ~0U); p += 4;          // home time
	put2(p, 14); p += 2;           // channel count
	put2(p, 1); p += 2;            // ssid count
	memcpy(p, chanspecs, sizeof(chanspecs)); p += sizeof(chanspecs);
	p += 4 + 32;                   // ssids[0] wildcard

	wlcmdint(WLC_SET_PASSIVE_SCAN, 0);
	IOReturn r = wlsetvar("escan", params, (uint32_t)(p - params));
	IOLockLock(fEventLock);
	if (r == kIOReturnSuccess) {
		uint64_t deadline;
		clock_interval_to_deadline(10000, kMillisecondScale, &deadline);
		while (!fScanDone) {
			if (IOLockSleepDeadline(fEventLock, &fScanDone, deadline, THREAD_UNINT) == THREAD_TIMED_OUT) {
				break;
			}
		}
	}
	fScanning = false;
	memcpy(out, fScan, sizeof(*out));
	IOLockUnlock(fEventLock);
	return r;
}

IOReturn
BCM43430Wifi::join(const wifi_join *req)
{
	if (fState != kWifiStateUp) {
		return kIOReturnNotReady;
	}
	char ssid[33], pass[65];
	strlcpy(ssid, req->ssid, sizeof(ssid));
	strlcpy(pass, req->passphrase, sizeof(pass));
	size_t sl = strlen(ssid), pl = strlen(pass);
	if (sl == 0) {
		bzero(pass, sizeof(pass));
		return kIOReturnBadArgument;
	}
	bool wpa = pl > 0;
	bool hexOk = true;
	for (size_t i = 0; pl == 64 && i < pl; i++) {
		char ch = pass[i];
		hexOk = hexOk && ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'));
	}
	if (wpa && (pl < 8 || pl > 64 || !hexOk)) {
		bzero(pass, sizeof(pass));
		setError("passphrase must be 8-63 characters (or 64 hex digits)");
		return kIOReturnBadArgument;
	}

	wlcmd(WLC_DISASSOC, true, NULL, 0, NULL, 0);
	IOLockLock(fEventLock);
	fJoinResult = -1;
	fJoinWpa = wpa;
	fKeyed = false;
	fLinkUp = false;
	fAssociated = false;
	fPskStatus = -1;
	strlcpy(fSsid, ssid, sizeof(fSsid));
	IOLockUnlock(fEventLock);
	updateLink();

	// brcmfmac cfg80211.c connect path order: wsec, wpa_auth, sup_wpa, PMK,
	// then SSID (wifi-plan.md review B4).
	IOReturn r;
	if (wpa) {
		r = wlsetint("wsec", 4);                     // AES
		if (r == kIOReturnSuccess) r = wlsetint("wpa_auth", 0x80);   // WPA2_AUTH_PSK
		if (r == kIOReturnSuccess) r = wlsetint("sup_wpa", 1);
		if (r == kIOReturnSuccess) {
			// wsec_pmk_t {u16 key_len; u16 flags; u8 key[64]}: passphrase
			// with flags 1 (BRCMF_WSEC_PASSPHRASE), or the 64-hex-digit PSK
			// as text with flags 0 (brcmf_set_pmk()).
			uint8_t pmk[68];
			bzero(pmk, sizeof(pmk));
			put2(pmk, (uint32_t)pl);
			put2(pmk + 2, pl == 64 ? 0 : 1);
			memcpy(pmk + 4, pass, pl);
			r = wlcmd(WLC_SET_WSEC_PMK, true, pmk, sizeof(pmk), NULL, 0);
			bzero(pmk, sizeof(pmk));
		}
		bzero(pass, sizeof(pass));
		if (r != kIOReturnSuccess) {
			setError("firmware rejected the WPA2 setup (sup_wpa/PMK)");
			return r;
		}
	} else {
		wlsetint("wsec", 0);
		wlsetint("wpa_auth", 0);
		wlsetint("sup_wpa", 0);
	}
	wlcmdint(WLC_SET_INFRA, 1);
	wlcmdint(WLC_SET_AUTH, 0);

	uint8_t ssidle[36];
	bzero(ssidle, sizeof(ssidle));
	put4(ssidle, (uint32_t)sl);
	memcpy(ssidle + 4, ssid, sl);
	r = wlcmd(WLC_SET_SSID, true, ssidle, sizeof(ssidle), NULL, 0);
	if (r != kIOReturnSuccess) {
		setError("SET_SSID failed");
		return r;
	}

	IOLockLock(fEventLock);
	uint64_t deadline;
	clock_interval_to_deadline(20000, kMillisecondScale, &deadline);
	bool timedOut = false;
	for (;;) {
		if (fJoinResult > 0) {
			break;                                  // association failed
		}
		if (fJoinResult == 0 && fAssociated) {
			break;                                  // done
		}
		if (wpa && fPskStatus == (int)kSupTimeout) {
			break;
		}
		if (IOLockSleepDeadline(fEventLock, &fJoinResult, deadline, THREAD_UNINT) == THREAD_TIMED_OUT) {
			timedOut = true;
			break;
		}
	}
	int result = fJoinResult;
	bool assoc = fAssociated;
	int psk = fPskStatus;
	IOLockUnlock(fEventLock);

	if (assoc) {
		IOLog("BCM43430Wifi: joined \"%s\" (%s)\n", ssid, wpa ? "WPA2-PSK" : "open");
		fLastError[0] = 0;
		return kIOReturnSuccess;
	}
	if (result > 0) {
		// E_SET_SSID status: 1 fail, 2 timeout, 3 no networks (brcmfmac
		// fweh.h BRCMF_E_STATUS_*).
		setError(result == 3 ? "network \"%s\" not found" : "association with \"%s\" failed", ssid);
	} else if (wpa) {
		setError("WPA2 handshake with \"%s\" did not complete (supplicant state %d) - wrong passphrase?",
		    ssid, psk);
	} else {
		setError("join \"%s\" %s", ssid, timedOut ? "timed out" : "failed");
	}
	wlcmd(WLC_DISASSOC, true, NULL, 0, NULL, 0);
	return kIOReturnError;
}

IOReturn
BCM43430Wifi::leave(void)
{
	if (fState != kWifiStateUp) {
		return kIOReturnNotReady;
	}
	IOReturn r = wlcmd(WLC_DISASSOC, true, NULL, 0, NULL, 0);
	IOLockLock(fEventLock);
	fLinkUp = fKeyed = fAssociated = false;
	fSsid[0] = 0;
	IOLockUnlock(fEventLock);
	updateLink();
	return r;
}

void
BCM43430Wifi::getStatus(wifi_status *out)
{
	bzero(out, sizeof(*out));
	out->state = fState;
	IOLockLock(fEventLock);
	out->associated = fAssociated ? 1 : 0;
	strlcpy(out->ssid, fSsid, sizeof(out->ssid));
	memcpy(out->bssid, fBssid, 6);
	IOLockUnlock(fEventLock);
	memcpy(out->mac, fMAC, 6);
	out->chipid = fChipId;
	out->chiprev = fChipRev;
	strlcpy(out->fwversion, fFwVersion, sizeof(out->fwversion));
	strlcpy(out->lasterror, fLastError, sizeof(out->lasterror));
	if (out->associated) {
		// brcmfmac fwil_types.h brcmf_scb_val_le {__le32 val; u8 ea[6]},
		// not packed, so sizeof is 12; the firmware rejects 10 bytes with
		// BCME_BADARG (-2), seen on real hardware.
		uint8_t scb[12];
		bzero(scb, sizeof(scb));
		if (wlcmd(WLC_GET_RSSI, false, scb, sizeof(scb), scb, sizeof(scb)) == kIOReturnSuccess) {
			out->rssi = (int32_t)get4(scb);
		}
	}
}

// ------------------------------------------------------- IONetworking glue

bool
BCM43430Wifi::createMedium(void)
{
	if (fMedium != NULL) {
		return true;
	}
	OSDictionary *dict = OSDictionary::withCapacity(1);
	if (dict == NULL) {
		return false;
	}
	fMedium = IONetworkMedium::medium(kIOMediumEthernetAuto, 0);
	bool ok = fMedium != NULL && IONetworkMedium::addMedium(dict, fMedium) && publishMediumDictionary(dict);
	dict->release();
	return ok;
}

void
BCM43430Wifi::updateLink(void)
{
	if (fMedium == NULL) {
		return;
	}
	// setLinkStatus() only takes the medium lock (IONetworkController.cpp),
	// so it is safe from threadMain and user-client threads alike.
	if (fAssociated) {
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, fMedium, 54 * 1000000ULL);
	} else {
		setLinkStatus(kIONetworkLinkValid, fMedium);
	}
}

IOReturn
BCM43430Wifi::getHardwareAddress(IOEthernetAddress *addr)
{
	memcpy(addr->bytes, fMAC, 6);
	return kIOReturnSuccess;
}

IOReturn
BCM43430Wifi::enable(IONetworkInterface *)
{
	fNetifEnabled = true;
	IOOutputQueue *q = getOutputQueue();
	if (q != NULL) {
		q->setCapacity(256);
		q->start();
	}
	updateLink();
	return kIOReturnSuccess;
}

IOReturn
BCM43430Wifi::disable(IONetworkInterface *)
{
	fNetifEnabled = false;
	IOOutputQueue *q = getOutputQueue();
	if (q != NULL) {
		q->stop();
		q->flush();
	}
	return kIOReturnSuccess;
}

bool
BCM43430Wifi::configureInterface(IONetworkInterface *netif)
{
	if (!IOEthernetController::configureInterface(netif)) {
		return false;
	}
	// As AppleUSBCDCECMData::configureInterface() does
	// (AppleUSBCDCECMData.cpp:1802): keep a pointer to the interface's
	// IONetworkStats buffer and count output in the driver.
	IONetworkData *nd = netif->getNetworkData(kIONetworkStatsKey);
	fNetStats = nd != NULL ? (IONetworkStats *)nd->getBuffer() : NULL;
	return true;
}

IOOutputQueue *
BCM43430Wifi::createOutputQueue(void)
{
	// A real output queue, so a full TX ring is back-pressure (Stall) and
	// not a drop: in this port kIOReturnOutputDropped (1) reaches userland
	// as errno 1, EPERM (DAR-326) - an ssh push died with "Operation not
	// permitted" on the first burst that filled the ring. IOBasicOutputQueue
	// calls outputPacket() from its own serialized context; threadMain calls
	// service() once a slot frees (AppleUSBCDCECMData does the same with
	// its gated queue).
	return IOBasicOutputQueue::withTarget(this, 256);
}

UInt32
BCM43430Wifi::outputPacket(mbuf_t m, void *)
{
	size_t len = mbuf_pkthdr_len(m);
	if (fState != kWifiStateUp || !fAssociated || len < 14 || len > kTxSlotBytes) {
		// Nowhere to send it. Dropped quietly, not reported as Dropped, for
		// the EPERM reason above (an Ethernet driver with no link drops the
		// same way).
		fTxDrops++;
		freePacket(m);
		return kIOReturnOutputSuccess;
	}
	IOInterruptState st = IOSimpleLockLockDisableInterrupt(fTxLock);
	bool full = (fTxHead - fTxTail) >= kTxSlots;
	if (full) {
		fTxStalled = true;
	}
	TxSlot *slot = full ? NULL : &fTxRing[fTxHead % kTxSlots];
	IOSimpleLockUnlockEnableInterrupt(fTxLock, st);
	if (slot == NULL) {
		wakeThread();
		return kIOReturnOutputStall;     // the queue keeps m and retries after service()
	}
	// Single producer: the output queue serializes outputPacket(), and only
	// it advances fTxHead, so the slot is ours until fTxHead moves.
	mbuf_copydata(m, 0, len, slot->data);
	slot->len = (uint32_t)len;
	freePacket(m);
	st = IOSimpleLockLockDisableInterrupt(fTxLock);
	fTxHead++;
	IOSimpleLockUnlockEnableInterrupt(fTxLock, st);
	wakeThread();
	return kIOReturnOutputSuccess;
}

IOReturn
BCM43430Wifi::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
	if (group != gIONetworkFilterGroup) {
		*filters = 0;
		return kIOReturnSuccess;
	}
	*filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast |
	    kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
	return kIOReturnSuccess;
}

IOReturn
BCM43430Wifi::setMulticastMode(bool active)
{
	if (fState != kWifiStateUp) {
		return kIOReturnSuccess;
	}
	return wlsetint("allmulti", active ? 1 : 0) == kIOReturnSuccess ? kIOReturnSuccess : kIOReturnError;
}

IOReturn
BCM43430Wifi::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
	if (fState != kWifiStateUp) {
		return kIOReturnSuccess;
	}
	// "mcast_list": {le32 count; u8 addr[count][6]} (brcmfmac
	// core.c _brcmf_set_multicast_list()). Over the firmware's limit, fall
	// back to allmulti.
	if (count > 32) {
		return setMulticastMode(true);
	}
	uint8_t buf[4 + 32 * 6];
	put4(buf, count);
	for (UInt32 i = 0; i < count; i++) {
		memcpy(buf + 4 + i * 6, addrs[i].bytes, 6);
	}
	return wlsetvar("mcast_list", buf, 4 + count * 6) == kIOReturnSuccess ? kIOReturnSuccess : kIOReturnError;
}

IOReturn
BCM43430Wifi::setPromiscuousMode(bool active)
{
	if (fState != kWifiStateUp) {
		return kIOReturnSuccess;
	}
	return wlcmdint(WLC_SET_PROMISC, active ? 1 : 0) == kIOReturnSuccess ? kIOReturnSuccess : kIOReturnError;
}
