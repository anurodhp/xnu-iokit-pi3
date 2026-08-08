/*
 * Freestanding port of iokit/BCM2837SDHCI/BCM2837SDHCI.cpp's register-level
 * protocol logic (read-only reference - that kext is NOT modified or used
 * here). Register offsets/bit values cross-checked directly against
 * third_party/qemu-zhuowei/hw/sd/sdhci-internal.h (the actual emulated
 * model) - see tools/sdhci_baremetal/README.md for the full comparison;
 * every offset matched with no discrepancies found.
 *
 * Differences from the kext, both purely mechanical (no protocol change):
 *   - IOLog -> uart_puts/uart_hex32
 *   - IODelay(50)-based busy loops -> plain iteration-bounded busy loops
 *     (no calibrated timer available this early/freestanding)
 *   - waitForIntStatus32's interrupt-driven sleep -> direct INT_STATUS
 *     polling (no interrupt controller set up in this harness at all)
 */
#include "sdhci.h"
#include "uart.h"

#define SDHCI_BASE 0x3F300000UL

#define REG8(off)  (*(volatile uint8_t  *)(SDHCI_BASE + (off)))
#define REG16(off) (*(volatile uint16_t *)(SDHCI_BASE + (off)))
#define REG32(off) (*(volatile uint32_t *)(SDHCI_BASE + (off)))

enum {
	kRegBlockSize      = 0x04,
	kRegBlockCount     = 0x06,
	kRegArgument       = 0x08,
	kRegTransferMode   = 0x0C,
	kRegCommand        = 0x0E,
	kRegResponse0      = 0x10,
	kRegBuffer         = 0x20,
	kRegPresentState   = 0x24,
	kRegPowerControl   = 0x29,
	kRegClockControl   = 0x2C,
	kRegSoftwareReset  = 0x2F,
	kRegIntStatus      = 0x30,
	kRegIntEnable      = 0x34,
	kRegSignalEnable   = 0x38,
};

enum {
	kPresentCmdInhibit  = 1U << 0,
	kPresentDatInhibit  = 1U << 1,
	kPresentBufReadRdy  = 1U << 11,
};

enum {
	kIntCmdComplete  = 1U << 0,
	kIntTransferDone = 1U << 1,
	kIntBufReadRdy   = 1U << 5,
	kIntErrorMask    = 0xFFFF8000,
};

enum {
	kCmdTransBlkCntEn = 1U << 1,
	kCmdTransRead     = 1U << 4,
};

enum {
	kCmdRespNone      = 0x00,
	kCmdRespLong      = 0x01,
	kCmdRespShort     = 0x02,
	kCmdRespShortBusy = 0x03,
	kCmdCRCCheckEn    = 1U << 3,
	kCmdIndexCheckEn  = 1U << 4,
	kCmdDataPresent   = 1U << 5,
};

#define POLL_ITERS 20000000U

static int
wait_clear32(uint32_t off, uint32_t mask)
{
	for (uint32_t i = 0; i < POLL_ITERS; i++) {
		if ((REG32(off) & mask) == 0) {
			return 1;
		}
	}
	return 0;
}

static int
wait_int_status(uint32_t mask, uint32_t *out)
{
	for (uint32_t i = 0; i < POLL_ITERS; i++) {
		uint32_t v = REG32(kRegIntStatus);
		if (v & mask) {
			*out = v & mask;
			/* Ack only the bits this call asked for, same as the
			 * kext's interruptOccurred() only acking `known`
			 * bits - NOT a blind write-back of the whole
			 * register. QEMU's model sets CMD_COMPLETE and
			 * BUFFER_READ_READY synchronously within the same
			 * COMMAND-register write (data_present commands),
			 * so a naive `REG32(kRegIntStatus) = v` here would
			 * silently ack-and-lose BUFFER_READ_READY before
			 * the caller's later, separate wait for it ever
			 * gets to see it. Bug found and fixed in this
			 * harness itself, not present in the kext. */
			REG32(kRegIntStatus) = v & mask;
			return 1;
		}
	}
	return 0;
}

int
sdhci_reset(void)
{
	REG8(kRegSoftwareReset) = 0x01;
	if (!wait_clear32(kRegSoftwareReset, 0x01)) {
		uart_puts("sdhci: software reset did not clear\n");
		return 0;
	}
	uint32_t ints = kIntCmdComplete | kIntTransferDone | kIntBufReadRdy | kIntErrorMask;
	REG32(kRegIntEnable) = ints;
	REG32(kRegSignalEnable) = ints;
	return 1;
}

int
sdhci_set_clock(void)
{
	REG16(kRegClockControl) = 0x0000;
	uint16_t clk = (0xFFU << 8) | 0x01;
	REG16(kRegClockControl) = clk;
	for (uint32_t i = 0; i < POLL_ITERS; i++) {
		if (REG16(kRegClockControl) & 0x02) {
			REG16(kRegClockControl) = clk | 0x04;
			return 1;
		}
	}
	uart_puts("sdhci: internal clock never stabilized\n");
	return 0;
}

static int
send_command(uint8_t cmdIndex, uint32_t arg, uint32_t respType, int hasData, uint32_t resp[4])
{
	if (!wait_clear32(kRegPresentState, kPresentCmdInhibit)) {
		uart_puts("sdhci: CMD_INHIBIT stuck before CMD");
		uart_dec(cmdIndex);
		uart_puts("\n");
		return 0;
	}
	if (hasData && !wait_clear32(kRegPresentState, kPresentDatInhibit)) {
		uart_puts("sdhci: DAT_INHIBIT stuck\n");
		return 0;
	}

	REG32(kRegArgument) = arg;

	uint16_t flags = (uint16_t)(respType & kCmdRespShortBusy);
	if (respType == kCmdRespShort || respType == kCmdRespShortBusy) {
		flags |= kCmdCRCCheckEn | kCmdIndexCheckEn;
	} else if (respType == kCmdRespLong) {
		flags |= kCmdCRCCheckEn;
	}
	if (hasData) {
		flags |= kCmdDataPresent;
	}

	REG16(kRegCommand) = (uint16_t)((cmdIndex << 8) | flags);

	uint32_t status;
	if (!wait_int_status(kIntCmdComplete | kIntErrorMask, &status)) {
		uart_puts("sdhci: CMD");
		uart_dec(cmdIndex);
		uart_puts(" timed out\n");
		return 0;
	}
	if (status & kIntErrorMask) {
		uart_puts("sdhci: CMD");
		uart_dec(cmdIndex);
		uart_puts(" error, INT_STATUS=");
		uart_hex32(status);
		uart_puts("\n");
		return 0;
	}

	if (resp != 0) {
		if (respType == kCmdRespLong) {
			uint32_t hw[4];
			hw[0] = REG32(kRegResponse0 + 0x0);
			hw[1] = REG32(kRegResponse0 + 0x4);
			hw[2] = REG32(kRegResponse0 + 0x8);
			hw[3] = REG32(kRegResponse0 + 0xC);
			resp[0] = hw[3];
			resp[1] = hw[2];
			resp[2] = hw[1];
			resp[3] = hw[0];
			for (int i = 0; i < 4; i++) {
				resp[i] <<= 8;
				if (i != 3) {
					resp[i] |= resp[i + 1] >> 24;
				}
			}
		} else {
			resp[0] = REG32(kRegResponse0);
		}
	}
	return 1;
}

static uint32_t
unstuff_bits(const uint32_t resp[4], int start, int size)
{
	uint32_t mask = (size < 32) ? ((1U << size) - 1) : 0xFFFFFFFFU;
	int off = 3 - (start / 32);
	int shift = start & 31;
	uint32_t res = resp[off] >> shift;
	if (size + shift > 32 && off > 0) {
		res |= resp[off - 1] << ((32 - shift) % 32);
	}
	return res & mask;
}

void
sdhci_power_on(void)
{
	REG8(kRegPowerControl) = 0x0F;
}

int
sdhci_card_identify(sdhci_state_t *st)
{
	uint32_t resp[4];

	st->card_present = 0;
	st->card_is_sdhc = 0;
	st->rca = 0;
	st->block_count = 0;

	if (!send_command(0, 0, kCmdRespNone, 0, 0)) {
		uart_puts("sdhci: CMD0 failed\n");
		return 0;
	}

	int isV2 = send_command(8, 0x1AA, kCmdRespShort, 0, resp) && ((resp[0] & 0xFF) == 0xAA);
	if (!isV2) {
		uart_puts("sdhci: CMD8 failed/mismatch\n");
		return 0;
	}

	uint32_t acmd41Resp = 0;
	int ready = 0;
	for (int attempt = 0; attempt < 100; attempt++) {
		uint32_t r55[4], r41[4];
		if (!send_command(55, 0, kCmdRespShort, 0, r55)) {
			break;
		}
		if (!send_command(41, 0x40FF8000, kCmdRespShort, 0, r41)) {
			break;
		}
		acmd41Resp = r41[0];
		if (acmd41Resp & 0x80000000) {
			ready = 1;
			break;
		}
		for (volatile uint32_t d = 0; d < 200000; d++) {
			;
		}
	}
	if (!ready) {
		uart_puts("sdhci: ACMD41 never went ready\n");
		return 0;
	}
	st->card_is_sdhc = (acmd41Resp & 0x40000000) != 0;

	if (!send_command(2, 0, kCmdRespLong, 0, resp)) {
		uart_puts("sdhci: CMD2 failed\n");
		return 0;
	}

	if (!send_command(3, 0, kCmdRespShort, 0, resp)) {
		uart_puts("sdhci: CMD3 failed\n");
		return 0;
	}
	st->rca = resp[0] >> 16;

	if (!send_command(9, st->rca << 16, kCmdRespLong, 0, resp)) {
		uart_puts("sdhci: CMD9 failed\n");
		return 0;
	}
	uint32_t csdStruct = unstuff_bits(resp, 126, 2);
	if (csdStruct == 1) {
		uint32_t cSize = unstuff_bits(resp, 48, 22);
		st->block_count = ((uint64_t)cSize + 1) * 1024;
	} else {
		uart_puts("sdhci: CSD v1 (SDSC) unsupported\n");
		return 0;
	}

	if (!send_command(7, st->rca << 16, kCmdRespShortBusy, 0, 0)) {
		uart_puts("sdhci: CMD7 failed\n");
		return 0;
	}

	if (!send_command(16, 512, kCmdRespShort, 0, 0)) {
		uart_puts("sdhci: CMD16 failed\n");
		return 0;
	}
	REG16(kRegBlockSize) = 512;

	st->card_present = 1;
	uart_puts("sdhci: card ready, RCA=");
	uart_hex32(st->rca);
	uart_puts(" SDHC=");
	uart_dec(st->card_is_sdhc);
	uart_puts(" blocks=");
	uart_dec((uint32_t)st->block_count);
	uart_puts("\n");
	return 1;
}

int
sdhci_read_block(sdhci_state_t *st, uint64_t block, void *buf512)
{
	(void)st;
	uint32_t *dst = (uint32_t *)buf512;
	uint32_t arg = st->card_is_sdhc ? (uint32_t)block : (uint32_t)(block * 512);

	REG16(kRegTransferMode) = kCmdTransBlkCntEn | kCmdTransRead;
	REG16(kRegBlockCount) = 1;

	if (!send_command(17, arg, kCmdRespShort, 1, 0)) {
		return 0;
	}

	uint32_t status;
	if (!wait_int_status(kIntBufReadRdy | kIntErrorMask, &status)) {
		uart_puts("sdhci: read timed out waiting BUFFER_READ_READY\n");
		return 0;
	}
	if (status & kIntErrorMask) {
		uart_puts("sdhci: read error, INT_STATUS=");
		uart_hex32(status);
		uart_puts("\n");
		return 0;
	}

	for (uint32_t w = 0; w < 512 / 4; w++) {
		dst[w] = REG32(kRegBuffer);
	}

	uint32_t xferStatus;
	if (!wait_int_status(kIntTransferDone | kIntErrorMask, &xferStatus)) {
		uart_puts("sdhci: read did not reach TRANSFER_COMPLETE\n");
		return 0;
	}
	if (xferStatus & kIntErrorMask) {
		uart_puts("sdhci: read error at TRANSFER_COMPLETE\n");
		return 0;
	}
	return 1;
}
