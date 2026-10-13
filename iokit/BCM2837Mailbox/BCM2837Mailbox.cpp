#include "BCM2837Mailbox.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSAtomic.h>

/*
 * Mailbox register block.
 *
 * Physical base 0x3f00b880, length 0x40. Cross-checked three ways rather
 * than taken from memory:
 *   - devicetrees/rpi3.dts's "mbox" node: reg = <0xb880 0 0x40 0>, under
 *     "arm-io" whose ranges translate child 0 -> parent 0x3f000000
 *     (rpi3-realhw.dts #includes rpi3.dts, so real hardware has the same
 *     node - it is not a QEMU-only fiction);
 *   - pexpert/arm/pe_init.c's bcm2837_video_init(), which resolves that
 *     exact node and drives the same registers;
 *   - third_party/qemu-zhuowei's hw/arm/bcm2835_peripherals.c maps the
 *     mailbox at ARMCTRL_0_SBM_OFFSET (ARM_OFFSET 0xB000 + 0x800 =
 *     0xB800) and hw/misc/bcm2835_mbox.c masks the offset to 0xff, so
 *     MAIL0_READ 0x80 / MAIL0_STATUS 0x98 / MAIL1_WRITE 0xa0 /
 *     MAIL1_STATUS 0xb8 land at 0xb880 + {0x00, 0x18, 0x20, 0x38}.
 *
 * Mapped directly by physical address, not through the published "mbox"
 * IOPlatformDevice nub: this class is a plain helper with no IOService
 * provider to call mapDeviceMemoryWithIndex() on, and BCM2837SDHCI::
 * pinmuxSDSlot() already sets the precedent for reaching a
 * non-provider register block this way.
 */
enum {
	kMailboxPhysBase = 0x3f00b880,
	kMailboxRegLength = 0x40,

	kRegRead        = 0x00 / sizeof(uint32_t), // MAIL0_RD  (VC -> ARM)
	kRegStatus      = 0x18 / sizeof(uint32_t), // MAIL0_STA
	kRegWrite       = 0x20 / sizeof(uint32_t), // MAIL1_WRT (ARM -> VC)
	kRegWriteStatus = 0x38 / sizeof(uint32_t), // MAIL1_STA

	kStatusFull  = 1U << 31,
	kStatusEmpty = 1U << 30,

	kChannelProperty = 8,
};

enum {
	kTagGetBoardMAC    = 0x00010003,
	kTagGetBoardSerial = 0x00010004,
	kTagGetClockRate   = 0x00030002,
	kTagSetClockState  = 0x00038001,
	kTagSetClockRate   = 0x00038002,
	kTagEnd            = 0x00000000,

	kCodeRequest       = 0x00000000,
	kCodeResponseOK    = 0x80000000,
};

/*
 * Flat uint32_t message layout, exactly as the wire protocol lays it out
 * (buf[0] = total size in bytes, buf[1] = request/response code, then a
 * run of {tag, value_buffer_size, req/resp_code, ...value words...}
 * records, terminated by a lone zero tag). Written as explicit word
 * indices rather than a C struct so the offsets are unambiguous - same
 * approach pe_init.c's bcm2837_video_init() takes, for the same reason.
 *
 * Sized to 16 words / 64 bytes with three zero words of tail padding
 * after the end tag. That padding is load-bearing, not cosmetic: the
 * firmware (and QEMU's emulation of it, hw/misc/bcm2835_property.c)
 * walks records only while `record_offset + 8 <= buffer_start +
 * total_size`, so a total_size trimmed exactly to the end tag would stop
 * the walk *before* the end tag is ever read.
 */
enum {
	kMsgIdxSize = 0,
	kMsgIdxCode = 1,

	kMsgIdxMacTag = 2, kMsgIdxMacBufSize = 3, kMsgIdxMacRespCode = 4,
	kMsgIdxMacWord0 = 5, kMsgIdxMacWord1 = 6,

	kMsgIdxSerialTag = 7, kMsgIdxSerialBufSize = 8, kMsgIdxSerialRespCode = 9,
	kMsgIdxSerialLo = 10, kMsgIdxSerialHi = 11,

	kMsgIdxEnd = 12,

	kMsgWords = 16,
};

/*
 * Single-tag message layout, used by the clock-rate query. Same framing
 * rules as the two-tag identity message above; the end tag sits at word 5
 * and the remaining words are the same load-bearing tail padding (the
 * firmware's record walk needs total_size to extend at least 8 bytes past
 * the end tag's own offset).
 */
enum {
	kOneIdxTag = 2, kOneIdxBufSize = 3, kOneIdxRespCode = 4,
	kOneIdxWord0 = 5, kOneIdxWord1 = 6,
	kOneIdxEnd = 7,
};

#define kMsgBytes (kMsgWords * (uint32_t)sizeof(uint32_t))
#define kMacByteOffset (kMsgIdxMacWord0 * sizeof(uint32_t))

/*
 * The VideoCore sees ARM DRAM through four 1GB aliases of its own bus
 * (0x00000000/0x40000000/0x80000000/0xc0000000), differing only in how
 * the GPU's own L1/L2 caches treat the access; 0xc0000000 is the
 * uncached one. Handing the firmware a plain ARM physical address (i.e.
 * the 0x00000000 alias) asks it to read our buffer through its L2, which
 * has no coherency with anything the CPU did. Every real client of this
 * interface - Linux's drivers/firmware/raspberrypi.c, u-boot, the
 * bare-metal reference code - uses the 0xc0000000 alias, and so does
 * this project's own BCM2837DWC2 for its DWC2 descriptor buffer
 * (kBCM2837BusAddressBase). QEMU aliases all four onto the same RAM
 * (hw/arm/bcm2835_peripherals.c's ram_alias[4] loop), so this is correct
 * in emulation too, just not observable there.
 *
 * pexpert/arm/pe_init.c's bcm2837_mbox_call() passes the raw physical
 * address instead and compensates with explicit CleanPoC_DcacheRegion()/
 * FlushPoC_DcacheRegion() around the call. That is a different, also
 * valid arrangement; it is not reused here because those cache-
 * maintenance routines are osfmk-internal and not reachable from a
 * standalone-compilable IOKit source (tools/build_kexts.sh), and because
 * the write-combine buffer mapping below removes the need for them.
 */
#define kBusAddressBase 0xc0000000ULL

/* Bounded, in microseconds - never spin forever waiting on the GPU. */
enum {
	kMailboxTimeoutUS = 100000, // 100ms
};

static IOLock * volatile sLock = NULL;
static volatile uint32_t *sRegs = NULL;

static bool sQueried = false;
static bool sHaveFirmwareMAC = false;
static uint8_t sFirmwareMAC[6];
static bool sHaveSerial = false;
static uint64_t sSerial = 0;

static IOLock *
mailboxLock(void)
{
	if (sLock == NULL) {
		IOLock *fresh = IOLockAlloc();
		if (fresh == NULL) {
			return NULL;
		}
		if (!OSCompareAndSwapPtr(NULL, fresh, (void * volatile *)&sLock)) {
			IOLockFree(fresh);
		}
	}
	return sLock;
}

/*
 * Maps the mailbox register block once and caches it.
 *
 * Deliberately never unmapped, for the same reason BCM2837SDHCI::
 * pinmuxSDSlot() never releases its GPIO mapping: tearing down a bare
 * IODeviceMemory::withRange()+map() with no IOService provider behind it
 * takes a confirmed crash in IOMemoryMap::free() ->
 * memoryReferenceFree() -> Mach IPC in this kernel-only environment. It
 * is 64 bytes, mapped at most once per boot.
 */
static volatile uint32_t *
mailboxRegs(void)
{
	if (sRegs != NULL) {
		return sRegs;
	}

	IODeviceMemory *devMem =
	    IODeviceMemory::withRange(kMailboxPhysBase, kMailboxRegLength);
	if (devMem == NULL) {
		IOLog("BCM2837Mailbox: IODeviceMemory::withRange failed\n");
		return NULL;
	}
	IOMemoryMap *map = devMem->map();
	devMem->release();
	if (map == NULL) {
		IOLog("BCM2837Mailbox: map() failed\n");
		return NULL;
	}

	sRegs = (volatile uint32_t *)map->getVirtualAddress();
	return sRegs;
}

/*
 * Submits one already-populated property message and blocks (bounded)
 * until the GPU replies into the same buffer.
 *
 * msgBusAddr must be 16-byte aligned: the low four bits of the value
 * written to MAIL1 are the channel number, not address bits.
 */
static bool
mailboxCall(volatile uint32_t *regs, uint32_t *msg, uint64_t msgBusAddr)
{
	if ((msgBusAddr & 0xF) != 0) {
		IOLog("BCM2837Mailbox: message buffer not 16-byte aligned (%#llx)\n",
		    msgBusAddr);
		return false;
	}

	/*
	 * The message buffer is Normal Non-Cacheable memory and the mailbox
	 * registers are Device memory; ARM64 does not order accesses to the
	 * two against each other, so without this the MAIL1 write could be
	 * observed by the GPU before the message words it refers to are. Same
	 * reasoning as BCM2837DWC2's descriptor/doorbell barriers.
	 */
	__asm__ volatile ("dsb sy" ::: "memory");

	uint32_t waited = 0;
	while (regs[kRegWriteStatus] & kStatusFull) {
		if (++waited > kMailboxTimeoutUS) {
			IOLog("BCM2837Mailbox: timed out waiting for MAIL1 space\n");
			return false;
		}
		IODelay(1);
	}

	regs[kRegWrite] = (uint32_t)(msgBusAddr & ~0xFULL) | kChannelProperty;

	waited = 0;
	for (;;) {
		while (regs[kRegStatus] & kStatusEmpty) {
			if (++waited > kMailboxTimeoutUS) {
				IOLog("BCM2837Mailbox: timed out waiting for GPU reply\n");
				return false;
			}
			IODelay(1);
		}
		uint32_t reply = regs[kRegRead];
		if ((reply & 0xF) == kChannelProperty) {
			break;
		}
		/*
		 * Some other channel's message. Nothing in this kernel produces
		 * one (pe_init.c's framebuffer call uses this same channel and
		 * has long since drained its own reply), but draining rather
		 * than aborting is the safe response either way.
		 */
	}

	/* Reply words were written by the GPU, not by this CPU - order the
	 * register read that announced them ahead of reading them. */
	__asm__ volatile ("dsb sy" ::: "memory");

	if (msg[kMsgIdxCode] != kCodeResponseOK) {
		IOLog("BCM2837Mailbox: firmware returned code %#x (expected %#x)\n",
		    msg[kMsgIdxCode], kCodeResponseOK);
		return false;
	}
	return true;
}

/*
 * One property-message buffer: the descriptor, its CPU-side words, and
 * the VideoCore bus address to hand the firmware. Shared by every query
 * below rather than open-coded per query - the allocation constraints are
 * subtle enough (see msgBufAlloc()) that having two copies of them would
 * be a real hazard, not just duplication.
 */
struct MailboxMsgBuf {
	IOBufferMemoryDescriptor *desc;
	uint32_t *words;
	uint64_t busAddr;
};

/*
 * Physically contiguous, 16-byte aligned, within the low 1GB the
 * 0xc0000000 bus alias can reach, and mapped write-combine (real ARM64
 * Normal Non-Cacheable) rather than kIOMapInhibitCache - the same choice,
 * for the same recorded reason, as BCM2837DWC2::initDMABuffer():
 * kIOMapInhibitCache maps Device-nGnRnE, which faults on any unaligned
 * multi-byte access, and this buffer is read/written with byte-granular
 * accesses (the MAC bytes below). kIOMemoryMapperNone keeps any IOMapper
 * out of the way so getPhysicalSegment() returns a true physical address.
 *
 * On success the buffer is zeroed and the caller only has to fill in its
 * own tag records.
 */
static bool
msgBufAlloc(struct MailboxMsgBuf *out)
{
	out->desc = NULL;
	out->words = NULL;
	out->busAddr = 0;

	IOBufferMemoryDescriptor *buf =
	    IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
		kernel_task,
		kIODirectionInOut | kIOMemoryPhysicallyContiguous |
		kIOMapWriteCombineCache | kIOMemoryMapperNone,
		kMsgBytes,
		0x3FFFFFFFULL & ~0xFULL);
	if (buf == NULL) {
		IOLog("BCM2837Mailbox: message buffer allocation failed\n");
		return false;
	}
	if (buf->prepare() != kIOReturnSuccess) {
		IOLog("BCM2837Mailbox: message buffer prepare() failed\n");
		buf->release();
		return false;
	}

	IOByteCount segLen = 0;
	IOPhysicalAddress phys = buf->getPhysicalSegment(0, &segLen, 0);
	if (phys == 0 || segLen < kMsgBytes || (phys & 0xF) != 0) {
		IOLog("BCM2837Mailbox: message buffer unusable (phys=%#llx seglen=%llu)\n",
		    (uint64_t)phys, (uint64_t)segLen);
		buf->complete();
		buf->release();
		return false;
	}

	out->desc = buf;
	out->words = (uint32_t *)buf->getBytesNoCopy();
	out->busAddr = kBusAddressBase | (uint64_t)phys;
	bzero(out->words, kMsgBytes);
	return true;
}

static void
msgBufFree(struct MailboxMsgBuf *b)
{
	if (b->desc != NULL) {
		b->desc->complete();
		b->desc->release();
		b->desc = NULL;
	}
	b->words = NULL;
	b->busAddr = 0;
}

/*
 * Runs the one board-identity query, at most once per boot, and caches
 * the results. Caller must hold the lock.
 */
static void
queryBoardIdentityLocked(void)
{
	if (sQueried) {
		return;
	}
	sQueried = true; // one attempt per boot, success or not

	volatile uint32_t *regs = mailboxRegs();
	if (regs == NULL) {
		return;
	}

	struct MailboxMsgBuf buf;
	if (!msgBufAlloc(&buf)) {
		return;
	}
	uint32_t *m = buf.words;

	m[kMsgIdxSize] = kMsgBytes;
	m[kMsgIdxCode] = kCodeRequest;

	m[kMsgIdxMacTag] = kTagGetBoardMAC;
	m[kMsgIdxMacBufSize] = 8; // 6 real bytes, padded to a word multiple
	m[kMsgIdxMacRespCode] = 0;

	m[kMsgIdxSerialTag] = kTagGetBoardSerial;
	m[kMsgIdxSerialBufSize] = 8;
	m[kMsgIdxSerialRespCode] = 0;

	m[kMsgIdxEnd] = kTagEnd;

	if (mailboxCall(regs, m, buf.busAddr)) {
		/*
		 * Per-tag response word: bit 31 set marks it as a response, the
		 * low bits are the byte length the firmware actually wrote.
		 */
		if ((m[kMsgIdxMacRespCode] & 0x80000000U) != 0 &&
		    (m[kMsgIdxMacRespCode] & 0x7FFFFFFFU) >= 6) {
			/*
			 * The six MAC bytes are written as raw bytes in wire order
			 * (QEMU: dma_memory_write() of macaddr.a; real firmware does
			 * the same), NOT as byte-swapped words - so read them as
			 * bytes, with no endian conversion.
			 */
			const uint8_t *macBytes = (const uint8_t *)m + kMacByteOffset;
			bool allZero = true, allFF = true;
			for (int i = 0; i < 6; i++) {
				if (macBytes[i] != 0x00) {
					allZero = false;
				}
				if (macBytes[i] != 0xFF) {
					allFF = false;
				}
			}
			// Reject the same three "obviously not a station address"
			// shapes SMSC9514Ethernet::start() already rejects for the
			// EEPROM path: all-zero, all-FF, or the multicast bit set.
			if (!allZero && !allFF && (macBytes[0] & 0x01) == 0) {
				for (int i = 0; i < 6; i++) {
					sFirmwareMAC[i] = macBytes[i];
				}
				sHaveFirmwareMAC = true;
			}
		}

		uint64_t serial = ((uint64_t)m[kMsgIdxSerialHi] << 32) |
		    (uint64_t)m[kMsgIdxSerialLo];
		if ((m[kMsgIdxSerialRespCode] & 0x80000000U) != 0 && serial != 0) {
			sSerial = serial;
			sHaveSerial = true;
		}
	}

	msgBufFree(&buf);
}

/*
 * Runs one "get clock rate" query. Caller must hold the lock.
 */
static bool
queryClockRateLocked(uint32_t clockId, uint32_t *outHz)
{
	volatile uint32_t *regs = mailboxRegs();
	if (regs == NULL) {
		return false;
	}

	struct MailboxMsgBuf buf;
	if (!msgBufAlloc(&buf)) {
		return false;
	}
	uint32_t *m = buf.words;

	m[kMsgIdxSize] = kMsgBytes;
	m[kMsgIdxCode] = kCodeRequest;

	m[kOneIdxTag] = kTagGetClockRate;
	// 8-byte value buffer: request is {clock id}, response is
	// {clock id, rate in Hz}. Both firmware and QEMU read the id from
	// value+12 (word 0) and write the rate to value+16 (word 1).
	m[kOneIdxBufSize] = 8;
	m[kOneIdxRespCode] = 0;
	m[kOneIdxWord0] = clockId;
	m[kOneIdxWord1] = 0;

	m[kOneIdxEnd] = kTagEnd;

	bool ok = false;
	if (mailboxCall(regs, m, buf.busAddr)) {
		uint32_t rate = m[kOneIdxWord1];
		if ((m[kOneIdxRespCode] & 0x80000000U) != 0 &&
		    (m[kOneIdxRespCode] & 0x7FFFFFFFU) >= 8 &&
		    rate != 0) {
			// A firmware that doesn't know this clock answers 0, which is
			// "no rate", not "0 Hz" - treated as failure so the caller
			// falls back rather than dividing by it.
			*outHz = rate;
			ok = true;
		} else {
			IOLog("BCM2837Mailbox: clock id %u: no usable rate "
			      "(resp=%#x rate=%u)\n", clockId,
			    m[kOneIdxRespCode], rate);
		}
	}

	msgBufFree(&buf);
	return ok;
}

/*
 * Runs one "set clock state" request (tag 0x00038001; Linux:
 * RPI_FIRMWARE_SET_CLOCK_STATE, include/soc/bcm2835/raspberrypi-firmware.h).
 * Request {clock id, state}: state bit 0 = on, bit 1 = "clock not present".
 * The response word 1 carries the resulting state. Caller holds the lock.
 */
static bool
setClockStateLocked(uint32_t clockId, bool on)
{
	volatile uint32_t *regs = mailboxRegs();
	if (regs == NULL) {
		return false;
	}
	struct MailboxMsgBuf buf;
	if (!msgBufAlloc(&buf)) {
		return false;
	}
	uint32_t *m = buf.words;

	m[kMsgIdxSize] = kMsgBytes;
	m[kMsgIdxCode] = kCodeRequest;
	m[kOneIdxTag] = kTagSetClockState;
	m[kOneIdxBufSize] = 8;
	m[kOneIdxRespCode] = 0;
	m[kOneIdxWord0] = clockId;
	m[kOneIdxWord1] = on ? 1 : 0;
	m[kOneIdxEnd] = kTagEnd;

	bool ok = false;
	if (mailboxCall(regs, m, buf.busAddr)) {
		uint32_t state = m[kOneIdxWord1];
		if ((m[kOneIdxRespCode] & 0x80000000U) != 0 && (state & 2) == 0 &&
		    (state & 1) == (on ? 1U : 0U)) {
			ok = true;
		} else {
			IOLog("BCM2837Mailbox: set clock %u %s: resp=%#x state=%#x\n",
			    clockId, on ? "on" : "off", m[kOneIdxRespCode], state);
		}
	}
	msgBufFree(&buf);
	return ok;
}

/*
 * Set clock rate (tag 0x00038002): request {clock id, rate Hz, skip_turbo},
 * response {clock id, rate Hz actually set}. A 12-byte value buffer, unlike
 * the two-word tags above. The layout is from the firmware interface as
 * remembered (u-boot's mbox.h has only the get-rate tag); QEMU's
 * bcm2835_property.c:128-133 just logs "set clock rates NYI" and never writes
 * the buffer, so on QEMU this returns the request unchanged.
 */
static bool
setClockRateLocked(uint32_t clockId, uint32_t hz, uint32_t *actualHz)
{
	volatile uint32_t *regs = mailboxRegs();
	if (regs == NULL) {
		return false;
	}
	struct MailboxMsgBuf buf;
	if (!msgBufAlloc(&buf)) {
		return false;
	}
	uint32_t *m = buf.words;

	m[kMsgIdxSize] = kMsgBytes;
	m[kMsgIdxCode] = kCodeRequest;
	m[2] = kTagSetClockRate;
	m[3] = 12;
	m[4] = 0;
	m[5] = clockId;
	m[6] = hz;
	m[7] = 0;               // skip_turbo
	m[8] = kTagEnd;

	bool ok = false;
	if (mailboxCall(regs, m, buf.busAddr)) {
		if ((m[4] & 0x80000000U) != 0 && (m[4] & 0x7FFFFFFFU) >= 8 && m[6] != 0) {
			if (actualHz) {
				*actualHz = m[6];
			}
			ok = true;
		} else {
			IOLog("BCM2837Mailbox: set clock %u rate %u: resp=%#x rate=%u\n",
			    clockId, hz, m[4], m[6]);
		}
	}
	msgBufFree(&buf);
	return ok;
}

bool
BCM2837Mailbox::setClockRateHz(uint32_t clockId, uint32_t hz, uint32_t *actualHz)
{
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	bool ok = setClockRateLocked(clockId, hz, actualHz);
	IOLockUnlock(lock);
	return ok;
}

bool
BCM2837Mailbox::setClockState(uint32_t clockId, bool on)
{
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	bool ok = setClockStateLocked(clockId, on);
	IOLockUnlock(lock);
	return ok;
}

bool
BCM2837Mailbox::propertyTagWords(uint32_t tag, uint32_t *words, unsigned n, uint32_t *respCode)
{
	// One tag with an n-word value buffer, in and out. Layout as the
	// single-tag message: header (2), tag/size/code (3), n values, end tag,
	// then at least 8 bytes of the load-bearing tail padding.
	if (words == NULL || n == 0 || 5 + n + 1 + 2 > kMsgWords) {
		return false;
	}
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	bool ok = false;
	volatile uint32_t *regs = mailboxRegs();
	struct MailboxMsgBuf buf;
	if (regs != NULL && msgBufAlloc(&buf)) {
		uint32_t *m = buf.words;
		bzero(m, kMsgBytes);
		m[kMsgIdxSize] = kMsgBytes;
		m[kMsgIdxCode] = kCodeRequest;
		m[kOneIdxTag] = tag;
		m[kOneIdxBufSize] = n * 4;
		m[kOneIdxRespCode] = 0;
		for (unsigned i = 0; i < n; i++) {
			m[kOneIdxWord0 + i] = words[i];
		}
		m[kOneIdxWord0 + n] = kTagEnd;
		if (mailboxCall(regs, m, buf.busAddr)) {
			if (respCode != NULL) {
				*respCode = m[kOneIdxRespCode];
			}
			for (unsigned i = 0; i < n; i++) {
				words[i] = m[kOneIdxWord0 + i];
			}
			ok = true;
		}
		msgBufFree(&buf);
	}
	IOLockUnlock(lock);
	return ok;
}

bool
BCM2837Mailbox::setExpGPIO(unsigned line, bool high)
{
	// rpi_exp_gpio_dir_out() (gpio-raspberrypi-exp.c): Linux requests the
	// BT shutdown line as an output (GPIOD_OUT_LOW), which configures it
	// with SET_GPIO_CONFIG {gpio, direction 1, polarity (kept from
	// GET_GPIO_CONFIG), term_en 0, term_pull_up 0, state}. Done the same
	// way here so the line is an output whatever the firmware left it as.
	uint32_t resp = 0;
	uint32_t get[5] = { 128 + line, 0, 0, 0, 0 };
	uint32_t polarity = 0;
	if (propertyTagWords(0x00030043, get, 5, &resp) && get[0] == 0) {
		polarity = get[2];
	}
	uint32_t set[6] = { 128 + line, 1, polarity, 0, 0, high ? 1U : 0U };
	if (!propertyTagWords(0x00038043, set, 6, &resp)) {
		IOLog("BCM2837Mailbox: expander GPIO %u: mailbox call failed\n", line);
		return false;
	}
	// Only the message-level code (checked in mailboxCall()) and the status
	// word count, as in Linux: rpi_firmware_property_list() checks buf[1]
	// alone (drivers/firmware/raspberrypi.c:121), never the tag's response
	// bit, which this firmware leaves 0 for these tags (seen on a real Pi
	// 3B, firmware Aug 2026: code 0, status 0, line switched).
	if (set[0] != 0) {
		IOLog("BCM2837Mailbox: expander GPIO %u: SET_GPIO_CONFIG refused (code %#x, status %#x)\n",
		    line, resp, set[0]);
		return false;
	}
	return true;
}

bool
BCM2837Mailbox::propertyTag2(uint32_t tag, uint32_t a, uint32_t b,
    uint32_t *respCode, uint32_t *ra, uint32_t *rb)
{
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	bool ok = false;
	volatile uint32_t *regs = mailboxRegs();
	struct MailboxMsgBuf buf;
	if (regs != NULL && msgBufAlloc(&buf)) {
		uint32_t *m = buf.words;
		m[kMsgIdxSize] = kMsgBytes;
		m[kMsgIdxCode] = kCodeRequest;
		m[kOneIdxTag] = tag;
		m[kOneIdxBufSize] = 8;
		m[kOneIdxRespCode] = 0;
		m[kOneIdxWord0] = a;
		m[kOneIdxWord1] = b;
		m[kOneIdxEnd] = kTagEnd;
		if (mailboxCall(regs, m, buf.busAddr)) {
			if (respCode != NULL) {
				*respCode = m[kOneIdxRespCode];
			}
			if (ra != NULL) {
				*ra = m[kOneIdxWord0];
			}
			if (rb != NULL) {
				*rb = m[kOneIdxWord1];
			}
			ok = true;
		}
		msgBufFree(&buf);
	}
	IOLockUnlock(lock);
	return ok;
}

bool
BCM2837Mailbox::getClockRateHz(uint32_t clockId, uint32_t *outHz)
{
	if (outHz == NULL) {
		return false;
	}
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	bool ok = queryClockRateLocked(clockId, outHz);
	IOLockUnlock(lock);
	return ok;
}

bool
BCM2837Mailbox::getBoardSerial(uint64_t *outSerial)
{
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	queryBoardIdentityLocked();
	bool have = sHaveSerial;
	uint64_t serial = sSerial;
	IOLockUnlock(lock);

	if (have && outSerial != NULL) {
		*outSerial = serial;
	}
	return have;
}

bool
BCM2837Mailbox::getBoardMACAddress(uint8_t outMAC[6])
{
	IOLock *lock = mailboxLock();
	if (lock == NULL) {
		return false;
	}
	IOLockLock(lock);
	queryBoardIdentityLocked();
	bool haveMAC = sHaveFirmwareMAC;
	bool haveSerial = sHaveSerial;
	uint8_t mac[6];
	for (int i = 0; i < 6; i++) {
		mac[i] = sFirmwareMAC[i];
	}
	uint64_t serial = sSerial;
	IOLockUnlock(lock);

	if (haveMAC) {
		for (int i = 0; i < 6; i++) {
			outMAC[i] = mac[i];
		}
		return true;
	}

	if (haveSerial) {
		// Raspberry Pi Foundation convention: OUI b8:27:eb followed by
		// the low three bytes of the board serial, most-significant
		// byte first - the same derivation the Foundation's own
		// firmware/u-boot performs.
		outMAC[0] = 0xb8;
		outMAC[1] = 0x27;
		outMAC[2] = 0xeb;
		outMAC[3] = (uint8_t)((serial >> 16) & 0xFF);
		outMAC[4] = (uint8_t)((serial >> 8) & 0xFF);
		outMAC[5] = (uint8_t)(serial & 0xFF);
		return true;
	}

	return false;
}

void
BCM2837Mailbox::primeBoardIdentity(void)
{
	uint64_t serial = 0;
	bool haveSerial = BCM2837Mailbox::getBoardSerial(&serial);

	uint8_t mac[6];
	bool haveMAC = BCM2837Mailbox::getBoardMACAddress(mac);

	if (haveSerial) {
		IOLog("BCM2837Mailbox: board serial 0x%016llx\n", serial);
	} else {
		IOLog("BCM2837Mailbox: no board serial from firmware (QEMU's "
		      "bcm2835_property.c has tag 0x00010004 unimplemented)\n");
	}

	if (haveMAC) {
		IOLog("BCM2837Mailbox: board MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		IOLog("BCM2837Mailbox: no board MAC available from the VideoCore "
		      "mailbox\n");
	}
}
