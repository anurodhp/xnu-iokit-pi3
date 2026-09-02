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
 * This class exists for exactly one such piece today: the board's real
 * Ethernet MAC address, needed by SMSC9514Ethernet (see DAR-144 /
 * docs/historic/backlog.md P2 item 8 - the LAN9514 on a Raspberry Pi has
 * no EEPROM populated, so the chip cannot tell the driver its own
 * address, and hardcoding one means every board on a segment presents
 * the identical MAC).
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
