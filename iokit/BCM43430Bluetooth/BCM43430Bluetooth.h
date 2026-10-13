/*
 * BCM43430Bluetooth - HCI-over-UART (H4) transport for the Raspberry Pi 3's
 * on-board CYW43438 / BCM43430A1 Bluetooth controller. docs/bluetooth-plan.md.
 *
 * Hardware: the PL011 "uart0" (DT node "bt-uart", ARM phys 0x3f201000, GPU
 * IRQ 57) on GPIO32/33 ALT3, no flow control on the 3B; BT_ON is firmware
 * expander GPIO 0 (BCM2837Mailbox::setExpGPIO). PL011 register use follows
 * Linux drivers/tty/serial/amba-pl011.c; the bring-up sequence follows Linux
 * drivers/bluetooth/btbcm.c and hci_bcm.c (citations at each step).
 *
 * Only runs with boot-arg bt=1. The controller stays powered off until
 * userland pushes the patch and calls up (BCM43430BluetoothUserClient).
 *
 * Threads and locks:
 *   - interruptOccurred() runs at primary interrupt level. It drains the RX
 *     FIFO into fRing, a single-producer/single-consumer byte ring: only it
 *     advances fRingHead and only the worker advances fRingTail, so no lock
 *     is needed (OSMemoryBarrier orders the data before the index).
 *   - The worker thread (threadMain) deframes H4 packets out of fRing. It
 *     hands a Command Complete/Status that matches the command in flight to
 *     sendCommand(), and queues everything else for readEvent(). It sleeps
 *     with a 10 ms timeout, so a wakeup lost at interrupt level (no lock can
 *     be taken there) costs latency, not data.
 *   - fEventLock (IOLock) covers the command handoff and the event queue.
 *   - fCmdLock serializes commands (one in flight); fOpLock serializes
 *     user-client operations (upload, up).
 */
#ifndef _BCM43430BLUETOOTH_H
#define _BCM43430BLUETOOTH_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <kern/thread.h>

#include "BCM43430BluetoothShared.h"

class BCM43430Bluetooth : public IOService
{
	OSDeclareDefaultStructors(BCM43430Bluetooth);
	friend class BCM43430BluetoothUserClient;

public:
	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	virtual bool init(OSDictionary *properties = NULL) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	// User-client entry points (BCM43430BluetoothShared.h).
	IOReturn uploadBegin(void);
	IOReturn uploadChunk(uint32_t offset, const void *data, uint32_t len);
	IOReturn up(void);
	IOReturn userCommand(const bt_command *cmd, bt_packet *out);
	IOReturn readEvent(uint32_t timeoutMs, bt_packet *out);
	void     getStatus(bt_status *out);
	bool     claimReader(void *owner);
	void     releaseReader(void *owner);

private:
	IOService *fProvider;
	IOMemoryMap *fRegMap;
	volatile void *fRegBase;
	uint32_t fUartClockHz;
	uint32_t fBaud;
	uint32_t fOperBaud;

	IOLock *fEventLock;
	IOLock *fCmdLock;
	IOLock *fOpLock;
	thread_t fThread;
	volatile bool fWake;

	uint32_t fState;
	char fLastError[96];

	// Patch upload (under fOpLock).
	uint8_t *fPatch;
	uint32_t fPatchLen;

	// RX ring (see the header comment).
	enum { kRingSize = 64 * 1024 };
	uint8_t *fRing;
	volatile uint32_t fRingHead;     // written by interruptOccurred() only
	volatile uint32_t fRingTail;     // written by the worker only
	volatile bool fFlushPending;     // flushRx() asked; the worker drops everything up to the head
	volatile uint32_t fRxBadAt;      // ring position just past a byte with a DR error flag

	// H4 deframer (worker only).
	uint8_t fPkt[2 + kBtPacketMax];
	uint32_t fPktType, fPktHave, fPktNeed;
	bool fPktInHeader;

	// Counters.
	volatile uint64_t fRxBytes, fTxBytes;
	volatile uint32_t fRxOverruns, fRxErrors, fRingDrops, fResyncs, fEventDrops;

	// Command in flight (under fEventLock).
	uint16_t fWantOpcode;            // 0 = none
	bool fRespReady;
	bt_packet *fResp;
	int fCmdCredits;                 // Num_HCI_Command_Packets from the last CC/CS

	// Event queue for userland (under fEventLock).
	enum { kEvqSlots = 64 };
	bt_packet *fEvq;
	uint32_t fEvqHead, fEvqTail;
	void *fReader;                   // the one user client allowed to read events

	// Controller identity, from up().
	uint8_t fBdaddr[6];              // display order
	uint8_t fHciVersion, fLmpVersion;
	uint16_t fHciRevision, fLmpSubversion;
	char fName[64];

	// PL011 registers (Linux include/linux/amba/serial.h).
	enum {
		kRegDR   = 0x00,
		kRegFR   = 0x18,
		kRegIBRD = 0x24,
		kRegFBRD = 0x28,
		kRegLCRH = 0x2c,
		kRegCR   = 0x30,
		kRegIFLS = 0x34,
		kRegIMSC = 0x38,
		kRegMIS  = 0x40,
		kRegICR  = 0x44,
	};
	enum {
		kDrOE = 1U << 11, kDrBE = 1U << 10, kDrPE = 1U << 9, kDrFE = 1U << 8,
		kFrTXFE = 1U << 7, kFrRXFF = 1U << 6, kFrTXFF = 1U << 5, kFrRXFE = 1U << 4, kFrBUSY = 1U << 3,
		kLcrhWlen8 = 3U << 5, kLcrhFEN = 1U << 4,
		kCrRXE = 1U << 9, kCrTXE = 1U << 8, kCrUARTEN = 1U << 0,
		kIntOE = 1U << 10, kIntBE = 1U << 9, kIntPE = 1U << 8, kIntFE = 1U << 7,
		kIntRT = 1U << 6, kIntTX = 1U << 5, kIntRX = 1U << 4,
		kIntErrors = kIntOE | kIntBE | kIntPE | kIntFE,
		kIflsRx4_8Tx4_8 = (2U << 3) | 2U,   // UART011_IFLS_RX4_8 | UART011_IFLS_TX4_8
	};

	uint32_t readReg(uint32_t off) { return *(volatile uint32_t *)((uintptr_t)fRegBase + off); }
	void writeReg(uint32_t off, uint32_t v) { *(volatile uint32_t *)((uintptr_t)fRegBase + off) = v; }

	void uartInit(uint32_t baud);
	bool uartSetBaud(uint32_t baud);
	void uartWrite(const uint8_t *p, uint32_t len);
	void flushRx(void);

	IOReturn sendCommand(uint16_t opcode, const void *params, uint8_t plen,
	    bt_packet *resp, uint32_t timeoutMs = 2000);
	IOReturn cmdStatusByte(uint16_t opcode, const void *params, uint8_t plen,
	    bt_packet *resp, uint32_t timeoutMs = 2000);
	IOReturn bringUp(void);
	IOReturn downloadPatch(void);

	void deframe(void);
	void dispatchPacket(uint32_t type, const uint8_t *p, uint32_t len);
	void setError(const char *fmt, ...) __printflike(2, 3);

	static void interruptOccurred(OSObject *target, void *refCon, IOService *nub, int source);
	static void threadEntry(void *arg, wait_result_t wr);
	void threadMain(void);
};

#endif /* _BCM43430BLUETOOTH_H */
