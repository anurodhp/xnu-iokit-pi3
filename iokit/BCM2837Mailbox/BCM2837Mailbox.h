/*
 * BCM2837Mailbox - minimal VideoCore mailbox property-channel client,
 * usable from IOKit driver code.
 *
 * ================================ SCOPE ================================
 * The BCM2837's ARM core and its VideoCore GPU talk over a pair of
 * hardware mailboxes at physical 0x3f00b880. Channel 8 ("property
 * channel") carries a tag-based request/response protocol implemented by
 * the closed-source VideoCore firmware (Linux:
 * drivers/firmware/raspberrypi.c, drivers/mailbox/bcm2835-mailbox.c;
 * QEMU: third_party/qemu-zhuowei's hw/misc/bcm2835_mbox.c +
 * hw/misc/bcm2835_property.c). Several pieces of board identity exist
 * ONLY behind that interface - there is no register, no fuse, and no
 * devicetree property this kernel can read instead.
 *
 * Two such pieces exist today:
 *
 *   - the board's real Ethernet MAC address, needed by SMSC9514Ethernet
 *     (see DAR-144 / docs/historic/backlog.md P2 item 8 - the LAN9514 on
 *     a Raspberry Pi has no EEPROM populated, so the chip cannot tell the
 *     driver its own address, and hardcoding one means every board on a
 *     segment presents the identical MAC);
 *
 *   - the EMMC/SDHCI controller's base clock frequency, needed by
 *     BCM2837SDHCI to compute a real SD clock divisor (DAR-103). That
 *     controller is `missing_caps` (Linux sdhci-iproc's bcm2835_data),
 *     so its CAPABILITIES register's base-clock field is explicitly
 *     untrustworthy, and the real rate is whatever VideoCore firmware
 *     programmed at boot - it varies by board and by config.txt's
 *     core_freq. Linux solves this the same way: bcm2835's sdhci DT node
 *     carries `clocks = <&clocks BCM2835_CLOCK_EMMC>`, and that clock
 *     provider (drivers/clk/bcm/clk-raspberrypi.c) is a client of this
 *     same mailbox interface, tag 0x00030002.
 *
 * There is already a real property-channel implementation in this kernel
 * - pexpert/arm/pe_init.c's bcm2837_mbox_call()/bcm2837_video_init(),
 * which asks the GPU for an HDMI framebuffer before PE_create_console().
 * It is deliberately not reused here: it is static to that file, it runs
 * in a pre-VM, pre-IOKit context with its own cache-maintenance and
 * ml_io_map() idioms, and its message buffer layout is hardcoded to the
 * five framebuffer tags it sends. This class is the IOKit-side
 * equivalent, written against IOKit-only APIs (IODeviceMemory,
 * IOBufferMemoryDescriptor) so it also compiles standalone under
 * tools/build_kexts.sh. The wire protocol itself was re-derived from
 * third_party/qemu-zhuowei's hw/misc/bcm2835_property.c rather than from
 * memory, and cross-checks byte-for-byte against pe_init.c's framing.
 *
 * =============================== THREADING ==============================
 * The query is performed at most once and cached; both call sites today
 * (BCM2837DWC2::start() priming it, then SMSC9514Ethernet::start()
 * consuming it) run sequentially on the same IOKit matching thread, so
 * they cannot race each other. The IOLock below is still real, not
 * decorative: two concurrent property calls would each write MAIL1 and
 * then each read MAIL0, and either could consume the other's reply. It
 * guarantees that stays true for any future caller.
 */

#ifndef _BCM2837MAILBOX_H
#define _BCM2837MAILBOX_H

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>

class BCM2837Mailbox {
public:
	/*
	 * Clock IDs for getClockRateHz(), as defined by the Raspberry Pi
	 * firmware mailbox property interface. Only the one this kernel
	 * actually asks for is listed; the numbering was read out of
	 * third_party/qemu-zhuowei's hw/misc/bcm2835_property.c (the
	 * "case 1: EMMC" arm of tag 0x00030002's clock-id switch), which
	 * matches Linux's RPI_FIRMWARE_EMMC_CLK_ID.
	 */
	enum {
		kClockIdEMMC = 1,
		// RPI_FIRMWARE_V3D_CLK_ID: the fifth entry of Linux's clock-id
		// enum (include/soc/bcm2835/raspberrypi-firmware.h:140-144,
		// EMMC=1, UART, ARM, CORE, V3D).
		kClockIdV3D = 5,
	};

	/*
	 * The firmware-programmed rate, in Hz, of one of the VideoCore's
	 * clocks (property tag 0x00030002, "get clock rate").
	 *
	 * Returns false if the mailbox call fails or the firmware answers
	 * with 0 - which is the real, documented "this clock is not
	 * configured / not known" answer, not a rate. On false, *outHz is
	 * untouched and the caller MUST fall back to whatever it does when it
	 * has no idea what the clock is; there is deliberately no
	 * "reasonable default" invented here, because a wrong-but-plausible
	 * base clock is exactly what produces an unsafe SD clock divisor
	 * (DAR-103).
	 *
	 * Not cached: unlike the board identity above, this is a per-clock-id
	 * query and callers ask for it once at start() anyway. The mailbox
	 * lock is still held across the call, so it cannot race the identity
	 * query or another caller.
	 */
	static bool getClockRateHz(uint32_t clockId, uint32_t *outHz);

	/*
	 * Gates one of the VideoCore's clocks on or off (property tag
	 * 0x00038001, "set clock state"). Returns true only if the firmware
	 * acknowledges the clock now being in the requested state. Used by
	 * BCM2837V3D's power-up sequence.
	 */
	static bool setClockState(uint32_t clockId, bool on);

	/*
	 * The VideoCore's 64-bit per-board serial number (property tag
	 * 0x00010004). Returns false if the mailbox call fails or the
	 * firmware returns zero - the latter is a real, expected case under
	 * QEMU, whose bcm2835_property.c logs "get board serial NYI" and
	 * leaves the response buffer untouched.
	 */
	static bool getBoardSerial(uint64_t *outSerial);

	/*
	 * The board's real Ethernet MAC address, in wire byte order.
	 *
	 * Prefers property tag 0x00010003 ("get board MAC address"), which is
	 * what the Raspberry Pi firmware itself answers with and what Linux's
	 * own downstream smsc95xx MAC setup reads
	 * (RPI_FIRMWARE_GET_BOARD_MAC_ADDRESS). On a real Pi that is the
	 * Foundation-assigned b8:27:eb:xx:xx:xx address; QEMU implements this
	 * tag too (returning the -netdev MAC), so this path is exercisable in
	 * emulation.
	 *
	 * Falls back to deriving one the way the Foundation's firmware does:
	 * the Foundation OUI b8:27:eb followed by the low three bytes of the
	 * board serial, most-significant first.
	 *
	 * Returns false if neither is available, in which case *outMAC is
	 * untouched and the caller must decide what to do (SMSC9514Ethernet
	 * falls back to a loudly-logged locally-administered placeholder).
	 */
	static bool getBoardMACAddress(uint8_t outMAC[6]);

	/*
	 * Performs the query (if it has not run yet) and logs its outcome.
	 * Callers that only want the log, not the value, use this.
	 *
	 * This exists because SMSC9514Ethernet::start() - the one real
	 * consumer of getBoardMACAddress() - is unreachable under QEMU:
	 * QEMU's usb-net is always a generic CDC/RNDIS device, never a
	 * LAN9514, so BCM2837DWC2::start()'s findEthernetFunction() finds
	 * nothing and the whole Ethernet block is skipped (see that call
	 * site's comment). Without this, the mailbox path would have no
	 * QEMU-reachable exercise at all and could only ever be verified on
	 * real hardware.
	 */
	static void primeBoardIdentity(void);
};

#endif /* _BCM2837MAILBOX_H */
