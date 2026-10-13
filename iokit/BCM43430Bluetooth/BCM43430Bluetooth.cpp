#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>

#include "BCM43430Bluetooth.h"
#include "BCM2837Mailbox.h"
#include "BCM2837GPIO.h"

#define super IOService
OSDefineMetaClassAndStructors(BCM43430Bluetooth, IOService)

// Mailbox clock id of the PL011's UARTCLK (Linux raspberrypi-firmware.h
// RPI_FIRMWARE_UART_CLK_ID = 2).
static const uint32_t kClockIdUart = 2;
// Firmware expander line of BT_ON on the Pi 3B (bcm2837-rpi-3-b.dts:26,
// shutdown-gpios = <&expgpio 0 ...>).
static const unsigned kExpGpioBtOn = 0;
// Controller power-on speed (hci_bcm.c:1303, init_speed = 115200).
static const uint32_t kInitBaud = 115200;
// Default operating speed: what the downstream Raspberry Pi kernel uses on
// the 3B, which has no flow control (bcm2710-rpi-3-b.dts:202-204,
// max-speed 921600). Boot-arg bt-baud= overrides it.
static const uint32_t kDefaultOperBaud = 921600;
// LMP subversion of the BCM43430A1 ROM (btbcm.c:512).
static const uint16_t kSubverBCM43430A1 = 0x2209;

// HCI opcodes used here.
enum : uint16_t {
	kOpReset              = 0x0c03,
	kOpSetEventMask       = 0x0c01,
	kOpReadLocalName      = 0x0c14,
	kOpReadLocalVersion   = 0x1001,
	kOpReadBdaddr         = 0x1009,
	kOpLeSetEventMask     = 0x2001,
	kOpBcmWriteBdaddr     = 0xfc01,   // btbcm.c:159
	kOpBcmUpdateBaud      = 0xfc18,   // hci_bcm.c:214
	kOpBcmDownloadMinidrv = 0xfc2e,   // btbcm.c:227
	kOpBcmUartClock       = 0xfc45,   // hci_bcm.c:195
	kOpBcmWriteRam        = 0xfc4c,
	kOpBcmLaunchRam       = 0xfc4e,
};

static inline uint16_t get2(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

IOService *
BCM43430Bluetooth::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	// Same defensive name check as BCM2837SDHost::probe().
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "bt-uart") != 0) {
		return NULL;
	}
	uint32_t bt = 0;
	if (!PE_parse_boot_argn("bt", &bt, sizeof(bt)) || bt == 0) {
		return NULL;
	}
	return result;
}

bool
BCM43430Bluetooth::init(OSDictionary *properties)
{
	if (!super::init(properties)) {
		return false;
	}
	fProvider = NULL;
	fRegMap = NULL;
	fRegBase = NULL;
	fUartClockHz = 0;
	fBaud = 0;
	fOperBaud = kDefaultOperBaud;
	fThread = NULL;
	fWake = false;
	fState = kBtStateDown;
	fLastError[0] = 0;
	fPatch = NULL;
	fPatchLen = 0;
	fRingHead = fRingTail = fRxBadAt = 0;
	fFlushPending = false;
	fPktType = fPktHave = fPktNeed = 0;
	fPktInHeader = false;
	fRxBytes = fTxBytes = 0;
	fRxOverruns = fRxErrors = fRingDrops = fResyncs = fEventDrops = 0;
	fWantOpcode = 0;
	fRespReady = false;
	fResp = NULL;
	fCmdCredits = 1;
	fEvqHead = fEvqTail = 0;
	fReader = NULL;
	bzero(fBdaddr, sizeof(fBdaddr));
	fHciVersion = fLmpVersion = 0;
	fHciRevision = fLmpSubversion = 0;
	fName[0] = 0;

	fEventLock = IOLockAlloc();
	fCmdLock = IOLockAlloc();
	fOpLock = IOLockAlloc();
	fRing = (uint8_t *)IOMalloc(kRingSize);
	fEvq = (bt_packet *)IOMalloc(sizeof(bt_packet) * kEvqSlots);
	return fEventLock && fCmdLock && fOpLock && fRing && fEvq;
}

void
BCM43430Bluetooth::free(void)
{
	if (fRegMap) fRegMap->release();
	if (fEventLock) IOLockFree(fEventLock);
	if (fCmdLock) IOLockFree(fCmdLock);
	if (fOpLock) IOLockFree(fOpLock);
	if (fRing) IOFree(fRing, kRingSize);
	if (fEvq) IOFree(fEvq, sizeof(bt_packet) * kEvqSlots);
	if (fPatch) IOFree(fPatch, kBtPatchMax);
	super::free();
}

bool
BCM43430Bluetooth::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	fProvider = provider;
	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM43430Bluetooth: cannot map the PL011\n");
		return false;
	}
	fRegBase = (volatile void *)fRegMap->getVirtualAddress();

	// No made-up fallback: a wrong UARTCLK gives a wrong divisor and a link
	// that silently never answers (same rule as BCM2837SDHCI's base clock,
	// DAR-103).
	if (!BCM2837Mailbox::getClockRateHz(kClockIdUart, &fUartClockHz) || fUartClockHz == 0) {
		IOLog("BCM43430Bluetooth: firmware did not report the UART clock\n");
		return false;
	}
	uint32_t baud = 0;
	if (PE_parse_boot_argn("bt-baud", &baud, sizeof(baud)) && baud >= 9600 && baud <= 3000000) {
		fOperBaud = baud;
	}

	// Quiet until up(): UART off, no interrupts.
	writeReg(kRegCR, 0);
	writeReg(kRegIMSC, 0);
	writeReg(kRegICR, 0x7ff);

	IOReturn r = provider->registerInterrupt(0, this, &BCM43430Bluetooth::interruptOccurred);
	if (r == kIOReturnSuccess) {
		r = provider->enableInterrupt(0);
	}
	if (r != kIOReturnSuccess) {
		IOLog("BCM43430Bluetooth: interrupt setup failed, %#x\n", r);
		provider->unregisterInterrupt(0);
		return false;
	}

	if (kernel_thread_start(&BCM43430Bluetooth::threadEntry, this, &fThread) != KERN_SUCCESS) {
		IOLog("BCM43430Bluetooth: could not start the worker thread\n");
		provider->disableInterrupt(0);
		provider->unregisterInterrupt(0);
		return false;
	}
	thread_deallocate(fThread);

	setProperty("IOUserClientClass", "BCM43430BluetoothUserClient");
	registerService();
	IOLog("BCM43430Bluetooth: started (UART clock %u Hz, operating speed %u)\n", fUartClockHz, fOperBaud);
	return true;
}

void
BCM43430Bluetooth::setError(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(fLastError, sizeof(fLastError), fmt, ap);
	va_end(ap);
	IOLog("BCM43430Bluetooth: %s\n", fLastError);
}

// ---------------------------------------------------------------- PL011

bool
BCM43430Bluetooth::uartSetBaud(uint32_t baud)
{
	// pl011_set_termios(): quot = DIV_ROUND_CLOSEST(uartclk * 4, baud), in
	// 1/64ths; IBRD = quot >> 6, FBRD = quot & 0x3f.
	uint64_t quot = ((uint64_t)fUartClockHz * 4 + baud / 2) / baud;
	uint32_t ibrd = (uint32_t)(quot >> 6), fbrd = (uint32_t)(quot & 0x3f);
	if (ibrd == 0 || ibrd > 0xffff) {
		setError("no PL011 divisor for %u baud from a %u Hz clock", baud, fUartClockHz);
		return false;
	}
	// Let the last byte leave before reprogramming.
	for (int i = 0; i < 10000 && (readReg(kRegFR) & kFrBUSY); i++) {
		IODelay(10);
	}
	uint32_t cr = readReg(kRegCR);
	writeReg(kRegCR, 0);
	// IBRD and FBRD, then LCRH: the divisor only latches on the LCRH write
	// (amba-pl011.c pl011_write_lcr_h() / set_termios order).
	writeReg(kRegIBRD, ibrd);
	writeReg(kRegFBRD, fbrd);
	writeReg(kRegLCRH, kLcrhWlen8 | kLcrhFEN);
	writeReg(kRegCR, cr ? cr : (kCrUARTEN | kCrTXE | kCrRXE));
	fBaud = baud;
	return true;
}

void
BCM43430Bluetooth::uartInit(uint32_t baud)
{
	writeReg(kRegCR, 0);
	writeReg(kRegIMSC, 0);
	writeReg(kRegICR, 0x7ff);
	writeReg(kRegIFLS, kIflsRx4_8Tx4_8);
	uartSetBaud(baud);
	writeReg(kRegCR, kCrUARTEN | kCrTXE | kCrRXE);
	// RX + RT as Linux (pl011_startup), plus the error interrupts so an
	// overrun is counted even when it arrives with no data to drain
	// (project choice: they only count and are cleared through ICR).
	writeReg(kRegIMSC, kIntRX | kIntRT | kIntErrors);
}

void
BCM43430Bluetooth::uartWrite(const uint8_t *p, uint32_t len)
{
	// Polled: commands are at most 259 bytes.
	for (uint32_t i = 0; i < len; i++) {
		for (int spin = 0; (readReg(kRegFR) & kFrTXFF) && spin < 100000; spin++) {
			IODelay(1);
		}
		writeReg(kRegDR, p[i]);
	}
	fTxBytes += len;
}

void
BCM43430Bluetooth::interruptOccurred(OSObject *target, void *, IOService *, int)
{
	// Primary interrupt level. Loop on MIS until it reads zero, as
	// pl011_int() does. RX/RT are cleared by draining the FIFO, never by an
	// ICR write (amba-pl011.c:1640 masks them out of it): QEMU raises RX
	// only on the transition to the trigger level, so clearing it after a
	// drain can strand bytes.
	BCM43430Bluetooth *self = (BCM43430Bluetooth *)target;
	bool got = false;
	for (int pass = 0; pass < 256; pass++) {    // AMBA_ISR_PASS_LIMIT, amba-pl011.c:51
		uint32_t mis = self->readReg(kRegMIS);
		if (mis == 0) {
			break;
		}
		if (mis & kIntErrors) {
			if (mis & kIntOE) {
				self->fRxOverruns++;
			}
			self->writeReg(kRegICR, mis & kIntErrors);
		}
		while (!(self->readReg(kRegFR) & kFrRXFE)) {
			uint32_t d = self->readReg(kRegDR);
			if (d & kDrOE) {
				self->fRxOverruns++;
			}
			if (d & (kDrFE | kDrPE | kDrBE)) {
				self->fRxErrors++;
			}
			if (d & (kDrOE | kDrFE | kDrPE | kDrBE)) {
				// The packet being received has lost or bad bytes: make the
				// deframer drop it at this ring position rather than eat
				// the next packet's header.
				self->fRxBadAt = self->fRingHead + 1;
			}
			uint32_t head = self->fRingHead;
			if (head - self->fRingTail >= kRingSize) {
				self->fRingDrops++;
				continue;
			}
			self->fRing[head % kRingSize] = (uint8_t)d;
			__atomic_thread_fence(__ATOMIC_RELEASE);
			self->fRingHead = head + 1;
			got = true;
		}
	}
	if (got) {
		IOLockWakeup(self->fEventLock, (void *)&self->fWake, true);
	}
}

void
BCM43430Bluetooth::flushRx(void)
{
	// Ask the worker to drop everything received so far and reset the
	// deframer, then wait for it (it owns fRingTail).
	__atomic_store_n(&fFlushPending, true, __ATOMIC_RELEASE);
	IOLockLock(fEventLock);
	fWake = true;
	IOLockWakeup(fEventLock, (void *)&fWake, true);
	IOLockUnlock(fEventLock);
	for (int i = 0; i < 100 && __atomic_load_n(&fFlushPending, __ATOMIC_ACQUIRE); i++) {
		IOSleep(1);
	}
}

// ---------------------------------------------------------------- worker

void
BCM43430Bluetooth::threadEntry(void *arg, wait_result_t)
{
	((BCM43430Bluetooth *)arg)->threadMain();
}

void
BCM43430Bluetooth::threadMain(void)
{
	for (;;) {
		deframe();
		IOLockLock(fEventLock);
		if (!fWake && fRingHead == fRingTail && !fFlushPending) {
			uint64_t deadline;
			clock_interval_to_deadline(10, kMillisecondScale, &deadline);
			IOLockSleepDeadline(fEventLock, (void *)&fWake, deadline, THREAD_UNINT);
		}
		fWake = false;
		IOLockUnlock(fEventLock);
	}
}

void
BCM43430Bluetooth::deframe(void)
{
	if (__atomic_load_n(&fFlushPending, __ATOMIC_ACQUIRE)) {
		// Flush to the current head, never to a saved value: the worker may
		// already have consumed past what flushRx() saw, and moving the
		// tail backwards would replay those bytes.
		fRingTail = fRingHead;
		fPktType = 0;
		__atomic_store_n(&fFlushPending, false, __ATOMIC_RELEASE);
	}
	uint32_t head = fRingHead;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	while (fRingTail != head) {
		uint8_t b = fRing[fRingTail % kRingSize];
		fRingTail++;
		fRxBytes++;
		if (fRingTail == fRxBadAt && fPktType != 0) {
			fResyncs++;
			fPktType = 0;
			continue;
		}
		if (fPktType == 0) {
			// The controller sends only events and ACL data; SCO is unused
			// and commands never come this way.
			if (b == kBtH4Event) {
				fPktType = kBtH4Event;
				fPktNeed = 2;
			} else if (b == kBtH4Acl) {
				fPktType = kBtH4Acl;
				fPktNeed = 4;
			} else {
				fResyncs++;
				continue;
			}
			fPktHave = 0;
			fPktInHeader = true;
			continue;
		}
		fPkt[fPktHave++] = b;
		if (fPktHave < fPktNeed) {
			continue;
		}
		if (fPktInHeader) {
			fPktInHeader = false;
			if (fPktType == kBtH4Event) {
				fPktNeed = 2 + fPkt[1];
			} else {
				uint32_t alen = get2(fPkt + 2);
				if (alen > kBtPacketMax - 4) {
					// A corrupt length must not swallow the stream.
					fResyncs++;
					fPktType = 0;
					continue;
				}
				fPktNeed = 4 + alen;
			}
			if (fPktHave < fPktNeed) {
				continue;
			}
		}
		dispatchPacket(fPktType, fPkt, fPktHave);
		fPktType = 0;
	}
}

void
BCM43430Bluetooth::dispatchPacket(uint32_t type, const uint8_t *p, uint32_t len)
{
	IOLockLock(fEventLock);
	if (type == kBtH4Event && len >= 2) {
		uint8_t code = p[0];
		bool isCC = code == 0x0e && len >= 5;   // {ncmd, opcode[2], ...}
		bool isCS = code == 0x0f && len >= 6;   // {status, ncmd, opcode[2]}
		if (isCC || isCS) {
			uint16_t op = isCC ? get2(p + 3) : get2(p + 4);
			fCmdCredits = isCC ? p[2] : p[3];
			if (op == 0) {
				// NOP Command Complete: only reports command credits.
				IOLockWakeup(fEventLock, &fRespReady, false);
				IOLockUnlock(fEventLock);
				return;
			}
			if (fWantOpcode != 0 && op == fWantOpcode && fResp != NULL) {
				fResp->type = (uint8_t)type;
				fResp->len = (uint16_t)len;
				memcpy(fResp->data, p, len);
				fRespReady = true;
				IOLockWakeup(fEventLock, &fRespReady, false);
				IOLockUnlock(fEventLock);
				return;
			}
		}
	}
	if (fReader != NULL) {
		if (fEvqHead - fEvqTail >= kEvqSlots) {
			fEventDrops++;
		} else {
			bt_packet *e = &fEvq[fEvqHead % kEvqSlots];
			e->type = (uint8_t)type;
			e->reserved = 0;
			e->len = (uint16_t)len;
			memcpy(e->data, p, len);
			fEvqHead++;
			IOLockWakeup(fEventLock, &fEvqHead, false);
		}
	}
	IOLockUnlock(fEventLock);
}

// ---------------------------------------------------------------- commands

IOReturn
BCM43430Bluetooth::sendCommand(uint16_t opcode, const void *params, uint8_t plen,
    bt_packet *resp, uint32_t timeoutMs)
{
	IOLockLock(fCmdLock);
	IOLockLock(fEventLock);
	// Num_HCI_Command_Packets = 0 means wait for a credit (normally a NOP
	// Command Complete). Give it a second, then try anyway.
	uint64_t deadline;
	clock_interval_to_deadline(1000, kMillisecondScale, &deadline);
	while (fCmdCredits <= 0) {
		if (IOLockSleepDeadline(fEventLock, &fRespReady, deadline, THREAD_UNINT) == THREAD_TIMED_OUT) {
			break;
		}
	}
	fWantOpcode = opcode;
	fRespReady = false;
	fResp = resp;
	if (fCmdCredits > 0) {
		fCmdCredits--;
	}
	IOLockUnlock(fEventLock);

	uint8_t buf[4 + 255];
	buf[0] = kBtH4Command;
	buf[1] = (uint8_t)opcode;
	buf[2] = (uint8_t)(opcode >> 8);
	buf[3] = plen;
	if (plen > 0) {
		memcpy(buf + 4, params, plen);
	}
	uartWrite(buf, 4 + plen);

	IOLockLock(fEventLock);
	clock_interval_to_deadline(timeoutMs, kMillisecondScale, &deadline);
	while (!fRespReady) {
		if (IOLockSleepDeadline(fEventLock, &fRespReady, deadline, THREAD_UNINT) == THREAD_TIMED_OUT) {
			break;
		}
	}
	bool ok = fRespReady;
	fWantOpcode = 0;
	fResp = NULL;
	fRespReady = false;
	if (!ok) {
		// A lost response must not leave the controller looking busy, and
		// a half-received packet must not swallow the next one. (Project
		// choice; Linux's hci_core.c command timeout similarly restores
		// the command credit.)
		fCmdCredits = 1;
		__atomic_store_n(&fFlushPending, true, __ATOMIC_RELEASE);
		fWake = true;
		IOLockWakeup(fEventLock, (void *)&fWake, true);
	}
	IOLockUnlock(fEventLock);
	IOLockUnlock(fCmdLock);
	return ok ? kIOReturnSuccess : kIOReturnTimeout;
}

IOReturn
BCM43430Bluetooth::cmdStatusByte(uint16_t opcode, const void *params, uint8_t plen,
    bt_packet *resp, uint32_t timeoutMs)
{
	IOReturn r = sendCommand(opcode, params, plen, resp, timeoutMs);
	if (r != kIOReturnSuccess) {
		setError("HCI command %#06x: no response", opcode);
		return r;
	}
	// Command Complete: {0e, plen, ncmd, op[2], status, ...};
	// Command Status: {0f, plen, status, ncmd, op[2]}.
	uint8_t status = resp->data[0] == 0x0e ? (resp->len > 5 ? resp->data[5] : 0xff) : resp->data[2];
	if (status != 0) {
		setError("HCI command %#06x failed, status %#x", opcode, status);
		return kIOReturnError;
	}
	return kIOReturnSuccess;
}

IOReturn
BCM43430Bluetooth::downloadPatch(void)
{
	// btbcm_patchram(): validate first (records {le16 opcode, u8 plen,
	// params}; "Patch is corrupted" if one runs past the end), so a bad
	// file never leaves the controller half-patched.
	uint32_t off = 0, n = 0;
	uint16_t lastOp = 0;
	while (off + 3 <= fPatchLen) {
		uint8_t plen = fPatch[off + 2];
		if (off + 3 + plen > fPatchLen) {
			setError("patch is corrupted at offset %u", off);
			return kIOReturnBadArgument;
		}
		lastOp = get2(fPatch + off);
		off += 3 + plen;
		n++;
	}
	if (off != fPatchLen || n == 0 || lastOp != kOpBcmLaunchRam) {
		setError("patch does not end with Launch_RAM (%u records, %u trailing bytes)", n, fPatchLen - off);
		return kIOReturnBadArgument;
	}

	bt_packet *resp = (bt_packet *)IOMalloc(sizeof(bt_packet));
	if (resp == NULL) {
		return kIOReturnNoMemory;
	}
	IOReturn r = cmdStatusByte(kOpBcmDownloadMinidrv, NULL, 0, resp);
	if (r == kIOReturnSuccess) {
		IOSleep(50);    // btbcm.c:237, after Download Minidrv
		for (off = 0; off < fPatchLen && r == kIOReturnSuccess; ) {
			uint16_t op = get2(fPatch + off);
			uint8_t plen = fPatch[off + 2];
			r = cmdStatusByte(op, fPatch + off + 3, plen, resp);
			off += 3 + plen;
		}
	}
	IOFree(resp, sizeof(bt_packet));
	if (r == kIOReturnSuccess) {
		IOSleep(250);   // btbcm.c:273, after Launch RAM
		IOLog("BCM43430Bluetooth: patch loaded (%u records, %u bytes)\n", n, fPatchLen);
	}
	return r;
}

IOReturn
BCM43430Bluetooth::bringUp(void)
{
	// docs/bluetooth-plan.md B3. Patch at 115200, then one speed switch:
	// the `no_early_set_baudrate` variant of hci_bcm.c's order.
	bt_packet *resp = (bt_packet *)IOMalloc(sizeof(bt_packet));
	if (resp == NULL) {
		return kIOReturnNoMemory;
	}
	IOReturn r = kIOReturnError;

	// GPIO32/33 ALT3, pulls OFF/UP (bcm283x.dtsi uart0_gpio32).
	BCM2837GPIO::setFunction(32, 2, BCM2837GPIO::kFSelAlt3);
	BCM2837GPIO::setPull(32, 1, BCM2837GPIO::kPullOff);
	BCM2837GPIO::setPull(33, 1, BCM2837GPIO::kPullUp);
	uartInit(kInitBaud);

	// Power cycle. hci_bcm.c bcm_gpio_set_power() sleeps 100-120 ms after
	// power-on (hci_bcm.c:301).
	if (!BCM2837Mailbox::setExpGPIO(kExpGpioBtOn, false)) {
		setError("firmware refused BT_ON (expander GPIO %u)", kExpGpioBtOn);
		goto out;
	}
	IOSleep(10);    // project choice: Linux's power-off path has no fixed wait
	if (!BCM2837Mailbox::setExpGPIO(kExpGpioBtOn, true)) {
		setError("firmware refused BT_ON high");
		r = kIOReturnError;
		goto out;
	}
	IOSleep(120);
	flushRx();
	IOLockLock(fEventLock);
	fCmdCredits = 1;
	IOLockUnlock(fEventLock);

	if ((r = cmdStatusByte(kOpReset, NULL, 0, resp)) != kIOReturnSuccess) {
		goto out;
	}
	IOSleep(100);   // btbcm_reset(), btbcm.c:294

	// btbcm_initialize() reads the version before patching (btbcm.c:589-634).
	if ((r = cmdStatusByte(kOpReadLocalVersion, NULL, 0, resp)) != kIOReturnSuccess) {
		goto out;
	}
	if (resp->len < 14 || get2(resp->data + 12) != kSubverBCM43430A1) {
		setError("controller is not a BCM43430A1 (LMP subversion %#x); not patching",
		    resp->len >= 14 ? get2(resp->data + 12) : 0);
		r = kIOReturnUnsupported;
		goto out;
	}

	if ((r = downloadPatch()) != kIOReturnSuccess) {
		goto out;
	}
	if ((r = cmdStatusByte(kOpReset, NULL, 0, resp)) != kIOReturnSuccess) {
		goto out;
	}
	IOSleep(100);   // btbcm_finalize() -> btbcm_reset()

	if (fOperBaud != kInitBaud) {
		// Check the host can do the speed before telling the controller.
		uint64_t q = ((uint64_t)fUartClockHz * 4 + fOperBaud / 2) / fOperBaud;
		if ((q >> 6) == 0 || (q >> 6) > 0xffff) {
			setError("no PL011 divisor for %u baud from a %u Hz clock", fOperBaud, fUartClockHz);
			r = kIOReturnUnsupported;
			goto out;
		}
		// bcm_set_baudrate(): Update_UART_Baud_Rate {le16 0, le32 baud};
		// 0xfc45 is only for speeds above 3 Mbaud (hci_bcm.c:185).
		uint8_t p[6] = { 0, 0, (uint8_t)fOperBaud, (uint8_t)(fOperBaud >> 8),
			(uint8_t)(fOperBaud >> 16), (uint8_t)(fOperBaud >> 24) };
		if ((r = cmdStatusByte(kOpBcmUpdateBaud, p, sizeof(p), resp)) != kIOReturnSuccess) {
			goto out;
		}
		if (!uartSetBaud(fOperBaud)) {
			r = kIOReturnError;
			goto out;
		}
		flushRx();
	}

	// Smoke check at the new speed. Linux adds no delay after the switch
	// (hci_bcm.c:624-626); the one retry is a project choice, in case the
	// first command went out while the controller was still switching.
	r = cmdStatusByte(kOpReadLocalVersion, NULL, 0, resp, 500);
	if (r != kIOReturnSuccess) {
		IOSleep(20);
		flushRx();
		r = cmdStatusByte(kOpReadLocalVersion, NULL, 0, resp);
	}
	if (r != kIOReturnSuccess) {
		setError("no answer at %u baud after the speed switch", fBaud);
		goto out;
	}
	if (resp->len >= 14) {
		fHciVersion = resp->data[6];
		fHciRevision = get2(resp->data + 7);
		fLmpVersion = resp->data[9];
		fLmpSubversion = get2(resp->data + 12);
	}

	if ((r = cmdStatusByte(kOpReadBdaddr, NULL, 0, resp)) != kIOReturnSuccess) {
		goto out;
	}
	if (resp->len < 12) {
		setError("short Read_BD_ADDR reply (%u bytes)", resp->len);
		r = kIOReturnError;
		goto out;
	}
	for (int i = 0; i < 6; i++) {
		fBdaddr[i] = resp->data[11 - i];    // wire order is little-endian
	}
	{
		// The chip defaults (btbcm.c:28,36). Linux only flags them; Raspberry
		// Pi OS gets a real address from its firmware via a DT override. Here
		// (a project choice, docs/bluetooth-plan.md B3 step 8): the board MAC
		// with the last byte + 1.
		static const uint8_t def1[6] = { 0x43, 0x43, 0xa1, 0x12, 0x1f, 0xac };
		static const uint8_t def2[6] = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa };
		uint8_t mac[6];
		if ((memcmp(fBdaddr, def1, 6) == 0 || memcmp(fBdaddr, def2, 6) == 0) &&
		    BCM2837Mailbox::getBoardMACAddress(mac)) {
			for (int i = 5; i >= 3 && ++mac[i] == 0; i--) {
			}
			uint8_t le[6];
			for (int i = 0; i < 6; i++) {
				le[i] = mac[5 - i];
			}
			if (cmdStatusByte(kOpBcmWriteBdaddr, le, 6, resp) == kIOReturnSuccess) {
				memcpy(fBdaddr, mac, 6);
			}
		}
	}

	if (cmdStatusByte(kOpReadLocalName, NULL, 0, resp) == kIOReturnSuccess && resp->len > 6) {
		uint32_t n = resp->len - 6;
		if (n > sizeof(fName) - 1) {
			n = sizeof(fName) - 1;
		}
		memcpy(fName, resp->data + 6, n);
		fName[n] = 0;
	}

	{
		// Default mask (bits 0-44) plus Extended Inquiry Result (46) and LE
		// Meta (61), which the default leaves out (Bluetooth Core spec,
		// Set_Event_Mask). LE mask 0x1f: the default, including advertising
		// reports.
		uint64_t mask = 0x00001FFFFFFFFFFFULL | (1ULL << 46) | (1ULL << 61);
		uint8_t m[8];
		for (int i = 0; i < 8; i++) {
			m[i] = (uint8_t)(mask >> (8 * i));
		}
		if ((r = cmdStatusByte(kOpSetEventMask, m, 8, resp)) != kIOReturnSuccess) {
			goto out;
		}
		uint8_t le[8] = { 0x1f, 0, 0, 0, 0, 0, 0, 0 };
		if ((r = cmdStatusByte(kOpLeSetEventMask, le, 8, resp)) != kIOReturnSuccess) {
			goto out;
		}
	}

	IOLog("BCM43430Bluetooth: up at %u baud, %02x:%02x:%02x:%02x:%02x:%02x, HCI %u.%#x LMP %u.%#x, \"%s\"\n",
	    fBaud, fBdaddr[0], fBdaddr[1], fBdaddr[2], fBdaddr[3], fBdaddr[4], fBdaddr[5],
	    fHciVersion, fHciRevision, fLmpVersion, fLmpSubversion, fName);
	r = kIOReturnSuccess;
out:
	if (r != kIOReturnSuccess) {
		// Leave it off, as Linux does when setup fails.
		writeReg(kRegIMSC, 0);
		writeReg(kRegCR, 0);
		BCM2837Mailbox::setExpGPIO(kExpGpioBtOn, false);
	}
	IOFree(resp, sizeof(bt_packet));
	return r;
}

// ------------------------------------------------------- user client side

IOReturn
BCM43430Bluetooth::uploadBegin(void)
{
	if (fState == kBtStateLoading) {
		return kIOReturnBusy;
	}
	if (fPatch == NULL) {
		fPatch = (uint8_t *)IOMalloc(kBtPatchMax);
		if (fPatch == NULL) {
			return kIOReturnNoMemory;
		}
	}
	fPatchLen = 0;
	fLastError[0] = 0;
	return kIOReturnSuccess;
}

IOReturn
BCM43430Bluetooth::uploadChunk(uint32_t offset, const void *data, uint32_t len)
{
	if (fPatch == NULL || offset > kBtPatchMax || len > kBtPatchMax - offset) {
		return kIOReturnBadArgument;
	}
	memcpy(fPatch + offset, data, len);
	if (offset + len > fPatchLen) {
		fPatchLen = offset + len;
	}
	return kIOReturnSuccess;
}

IOReturn
BCM43430Bluetooth::up(void)
{
	if (fPatch == NULL || fPatchLen == 0) {
		return kIOReturnBadArgument;
	}
	fState = kBtStateLoading;
	fLastError[0] = 0;
	IOReturn r = bringUp();
	fState = r == kIOReturnSuccess ? kBtStateUp : kBtStateFailed;
	return r;
}

IOReturn
BCM43430Bluetooth::userCommand(const bt_command *cmd, bt_packet *out)
{
	if (fState != kBtStateUp) {
		return kIOReturnNotReady;
	}
	switch (cmd->opcode) {
	case kOpReset:
	case kOpBcmUpdateBaud:      // would move the controller's speed but not ours
	case kOpBcmUartClock:
	case kOpBcmDownloadMinidrv:
	case kOpBcmWriteRam:
	case kOpBcmLaunchRam:
		return kIOReturnNotPermitted;
	}
	return sendCommand(cmd->opcode, cmd->params, cmd->plen, out);
}

bool
BCM43430Bluetooth::claimReader(void *owner)
{
	IOLockLock(fEventLock);
	bool ok = fReader == NULL || fReader == owner;
	if (ok && fReader == NULL) {
		fReader = owner;
		fEvqHead = fEvqTail = 0;
	}
	IOLockUnlock(fEventLock);
	return ok;
}

void
BCM43430Bluetooth::releaseReader(void *owner)
{
	IOLockLock(fEventLock);
	if (fReader == owner) {
		fReader = NULL;
		fEvqHead = fEvqTail = 0;
		IOLockWakeup(fEventLock, &fEvqHead, false);
	}
	IOLockUnlock(fEventLock);
}

IOReturn
BCM43430Bluetooth::readEvent(uint32_t timeoutMs, bt_packet *out)
{
	if (fState != kBtStateUp) {
		return kIOReturnNotReady;
	}
	IOLockLock(fEventLock);
	uint64_t deadline;
	clock_interval_to_deadline(timeoutMs, kMillisecondScale, &deadline);
	IOReturn r = kIOReturnSuccess;
	while (fEvqHead == fEvqTail) {
		int w = IOLockSleepDeadline(fEventLock, &fEvqHead, deadline, THREAD_ABORTSAFE);
		if (w == THREAD_TIMED_OUT) {
			r = kIOReturnTimeout;
			break;
		}
		if (w == THREAD_INTERRUPTED) {
			r = kIOReturnAborted;
			break;
		}
	}
	if (r == kIOReturnSuccess) {
		*out = fEvq[fEvqTail % kEvqSlots];
		fEvqTail++;
	}
	IOLockUnlock(fEventLock);
	return r;
}

void
BCM43430Bluetooth::getStatus(bt_status *out)
{
	bzero(out, sizeof(*out));
	out->state = fState;
	out->baud = fBaud;
	memcpy(out->bdaddr, fBdaddr, 6);
	out->hci_version = fHciVersion;
	out->hci_revision = fHciRevision;
	out->lmp_version = fLmpVersion;
	out->lmp_subversion = fLmpSubversion;
	strlcpy(out->name, fName, sizeof(out->name));
	out->rx_bytes = fRxBytes;
	out->tx_bytes = fTxBytes;
	out->rx_overruns = fRxOverruns;
	out->rx_errors = fRxErrors;
	out->rx_ring_drops = fRingDrops;
	out->rx_resyncs = fResyncs;
	out->event_drops = fEventDrops;
	strlcpy(out->lasterror, fLastError, sizeof(out->lasterror));
}
