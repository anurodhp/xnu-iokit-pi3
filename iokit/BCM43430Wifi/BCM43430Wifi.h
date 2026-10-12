/*
 * BCM43430Wifi - driver for the Raspberry Pi 3's on-board CYW43438 /
 * BCM43430A1 WiFi (FullMAC, SDIO), as an IOEthernetController (en1).
 * docs/wifi-plan.md, steps W3/W4.
 *
 * Chip side is a port of Plan 9's ether4330.c (R. Miller, as carried in
 * Circle addon/wlan/ether4330.c), with details from Linux brcmfmac
 * (sdio.c, chip.c, fwil.h, fwil_types.h, fweh.h, cfg80211.c) where
 * ether4330 is silent. Function names in comments refer to those files.
 *
 * Threads and locks:
 *   - fBusLock (IOLock) serializes every SDIO transaction sequence that
 *     must not interleave (backplane window + access, a whole frame).
 *   - One kernel thread (threadMain) owns receive: it waits for the card
 *     interrupt (or a 10 ms tick), drains frames, dispatches control
 *     responses / events / data, and transmits queued data frames when the
 *     firmware's credit window allows. inputPacket() is only ever called
 *     from it.
 *   - Control requests (ioctls/iovars) are made from user-client or IOKit
 *     threads, never from threadMain: they send a frame and sleep on
 *     fEventLock until threadMain hands back the matching response.
 *   - outputPacket() only copies the frame into a ring under fTxLock (a
 *     spinlock) and wakes the thread.
 * Firmware is pushed by userland through BCM43430WifiUserClient.
 */
#ifndef _BCM43430WIFI_H
#define _BCM43430WIFI_H

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOBasicOutputQueue.h>
#include <IOKit/IOLocks.h>
#include <kern/thread.h>

#include "BCM43430WifiShared.h"

class BCM2837SDIODevice;

class BCM43430Wifi : public IOEthernetController
{
	OSDeclareDefaultStructors(BCM43430Wifi);
	friend class BCM43430WifiUserClient;

public:
	virtual bool init(OSDictionary *properties = NULL) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual IOReturn getHardwareAddress(IOEthernetAddress *addr) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;
	virtual UInt32   outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;
	virtual IOOutputQueue *createOutputQueue(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;

	// User-client entry points (BCM43430WifiShared.h).
	IOReturn uploadBegin(void);
	IOReturn uploadChunk(uint32_t type, uint32_t offset, const void *data, uint32_t len);
	IOReturn boot(void);
	IOReturn scan(wifi_scan_results *out);
	IOReturn join(const wifi_join *req);
	IOReturn leave(void);
	void     getStatus(wifi_status *out);

private:
	BCM2837SDIODevice *fSDIO;
	IOLock *fBusLock;
	IOLock *fEventLock;          // control responses, join/scan state, thread wakeups
	IOLock *fCmdLock;            // one control request in flight at a time
	IOLock *fOpLock;             // one user-client operation (upload/boot/join/scan) at a time
	IOSimpleLock *fTxLock;
	thread_t fThread;
	volatile bool fWake;         // thread wakeup reason pending
	volatile bool fCardIrq;

	IOEthernetInterface *fNetif;
	IONetworkMedium *fMedium;
	bool fNetifEnabled;

	// Chip (ether4330 Ctlr).
	uint32_t fState;
	uint32_t fChipId, fChipRev, fArmCore;
	uint32_t fChipCommon, fArmCtl, fArmRegs, fD11Ctl, fSocramCtl, fSocramRegs, fSdRegs;
	uint32_t fSocramRev, fSdioRev, fSocramSize, fRamBase;
	uint32_t fWindow;            // current backplane window, ~0 = unknown
	uint32_t fFwBytes;
	uint8_t *fNvram; uint32_t fNvramLen;
	uint8_t *fClm;   uint32_t fClmLen;
	uint8_t fMAC[6];
	char fFwVersion[128];
	char fLastError[96];

	// SDPCM.
	uint8_t fTxSeq, fTxWindow, fFcMask;
	uint16_t fReqId;
	uint8_t *fRxBuf;             // threadMain only
	uint8_t *fCmdBuf;            // under fCmdLock
	uint8_t *fTxFrame;           // threadMain only

	// Control response handoff (under fEventLock).
	uint16_t fRespWant;
	bool     fRespReady;
	uint32_t fRespStatus;
	uint8_t *fRespBuf;
	uint32_t fRespLen;

	// Join / link (under fEventLock).
	int      fJoinResult;        // -1 pending, 0 ok, >0 E_SET_SSID status
	bool     fJoinWpa;
	bool     fKeyed;
	bool     fLinkUp;
	bool     fAssociated;
	int      fPskStatus;         // -1 none yet, else E_PSK_SUP status
	char     fSsid[33];
	uint8_t  fBssid[6];

	// Scan (under fEventLock).
	bool     fScanning;
	bool     fScanDone;
	wifi_scan_results *fScan;

	// TX ring (under fTxLock).
	enum { kTxSlots = 32, kTxSlotBytes = 1600 };
	struct TxSlot { uint32_t len; uint8_t data[kTxSlotBytes]; };
	TxSlot *fTxRing;
	uint32_t fTxHead, fTxTail;
	uint32_t fTxDrops;
	volatile bool fTxStalled;    // outputPacket() returned Stall; restart the queue when a slot frees

	// SDIO / backplane primitives (callers hold fBusLock).
	bool cfgw(uint32_t off, uint8_t v);
	bool cfgr(uint32_t off, uint8_t *v);
	uint8_t cfgr8(uint32_t off);
	bool cfgreadl(uint32_t off, uint32_t *v);
	bool cfgwritel(uint32_t off, uint32_t v);
	bool sbwindow(uint32_t addr);
	bool sbmem(bool write, uint8_t *buf, uint32_t len, uint32_t off);
	void sbdisable(uint32_t regs, uint32_t pre, uint32_t ioctl);
	void sbreset(uint32_t regs, uint32_t pre, uint32_t ioctl);

	bool sdioInit(void);
	bool sbInit(void);
	bool coreScan(uint32_t erom);
	bool ramScan(void);
	bool sbEnable(void);
	bool probeChip(void);

	// Frames.
	bool writeFrame(uint8_t *frame, uint32_t len);
	int  readFrame(void);        // >0 length in fRxBuf, 0 none, <0 error
	void dispatchFrame(uint32_t len);
	void handleEvent(const uint8_t *p, uint32_t len);
	void handleScanResult(const uint8_t *p, uint32_t len, uint32_t status);
	void transmitPending(void);
	void serviceInterrupts(void);

	// Control (never from threadMain).
	IOReturn wlcmd(uint32_t cmd, bool set, const void *data, uint32_t dlen, void *out, uint32_t olen);
	IOReturn wlcmdint(uint32_t cmd, uint32_t val);
	IOReturn wlgetint(uint32_t cmd, uint32_t *val);
	IOReturn wlsetvar(const char *name, const void *val, uint32_t len);
	IOReturn wlgetvar(const char *name, void *out, uint32_t len);
	IOReturn wlsetint(const char *name, uint32_t val);
	IOReturn clmLoad(void);
	IOReturn wlInit(void);

	void updateLink(void);
	void setError(const char *fmt, ...) __printflike(2, 3);
	void wakeThread(void);
	static void cardInterrupt(OSObject *target);
	static void threadEntry(void *arg, wait_result_t wr);
	void threadMain(void);
	bool createMedium(void);
};

#endif /* _BCM43430WIFI_H */
