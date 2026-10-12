/*
 * BCM2837SDIODevice - the nub BCM2837SDHCI publishes when it runs the Arasan
 * controller as an SDIO host for the on-board BCM43438 (docs/wifi-plan.md,
 * W2). BCM43430Wifi matches on this class.
 *
 * It is a thin, blocking command interface. Every call takes the host's
 * transfer mutex for its whole duration, so calls from several threads are
 * serialized; none may be made from interrupt context. The SDIO card
 * protocol itself (CMD0/CMD5/CMD3/CMD7, CCCR setup) is the client's job,
 * as in Linux's mmc core vs. host split.
 *
 * Command encodings (SDIO Simplified Specification 3.00, 5.1 and 5.3):
 *   CMD52 IO_RW_DIRECT  arg = W<<31 | fn<<28 | RAW<<27 | addr<<9 | data
 *   CMD53 IO_RW_EXTENDED arg = W<<31 | fn<<28 | BLK<<27 | OP<<26 | addr<<9 | count
 * R5 response: bits 15:8 flags (0x80 CRC error, 0x40 illegal command,
 * 0x08 error, 0x02 bad function, 0x01 out of range), bits 7:0 data.
 */
#ifndef _BCM2837SDIODEVICE_H
#define _BCM2837SDIODEVICE_H

#include <IOKit/IOService.h>

class BCM2837SDHCI;

class BCM2837SDIODevice : public IOService
{
	OSDeclareDefaultStructors(BCM2837SDIODevice);

public:
	typedef void (*CardInterruptHandler)(OSObject *target);

	enum {
		kRespNone = 0,
		kRespR1,      // also R5 (CMD52/53) and R6 (CMD3): CRC + index checked
		kRespR1b,     // CMD7
		kRespR4,      // CMD5: no CRC, no index
	};

	static BCM2837SDIODevice *withHost(BCM2837SDHCI *host);

	// Raw command without data. resp may be NULL.
	IOReturn command(uint8_t index, uint32_t arg, int respType, uint32_t *resp);

	// CMD52. Fails on a transport error or any R5 error flag.
	IOReturn readDirect(unsigned fn, uint32_t addr, uint8_t *out);
	IOReturn writeDirect(unsigned fn, uint32_t addr, uint8_t value);

	// CMD53. len must be a multiple of 4. Split as ether4330's sdiorwext():
	// up to 511 blocks per command in block mode, a remainder smaller than
	// one block in byte mode. incr=false for FIFO addresses (F2 frames).
	IOReturn readExtended(unsigned fn, uint32_t addr, bool incr, void *buf, uint32_t len);
	IOReturn writeExtended(unsigned fn, uint32_t addr, bool incr, const void *buf, uint32_t len);

	// The block size the card's FBR was programmed with (the client writes
	// the FBR itself); used for CMD53 splitting. Default 64 for every fn.
	void setFunctionBlockSize(unsigned fn, uint32_t size);

	// Host side only: the client must switch the card first (CCCR 0x07).
	IOReturn setBusWidth4(bool wide);
	IOReturn setClock(uint32_t hz, uint32_t *actualHz);

	// The handler runs at primary interrupt level with the card interrupt
	// already masked; it may only wake a thread. Re-arm with
	// enableCardInterrupt(true) after servicing the card.
	void setCardInterruptHandler(CardInterruptHandler handler, OSObject *target);
	void enableCardInterrupt(bool on);

private:
	BCM2837SDHCI *fHost;
	uint32_t fBlockSize[8];

	IOReturn extended(bool write, unsigned fn, uint32_t addr, bool incr, uint8_t *buf, uint32_t len);
};

#endif /* _BCM2837SDIODEVICE_H */
