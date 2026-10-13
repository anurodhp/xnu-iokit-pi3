/*
 * User-client ABI between BCM43430Bluetooth (kernel) and /usr/sbin/bt
 * (tools/userland_staging/bt.c). Plain C, fixed-size structs, no pointers,
 * so both sides compile it unchanged. docs/bluetooth-plan.md, step B4.
 */
#ifndef _BCM43430BLUETOOTHSHARED_H
#define _BCM43430BLUETOOTHSHARED_H

#include <stdint.h>

#define kBCM43430BluetoothClass "BCM43430Bluetooth"

enum {
	kBtMethodUploadBegin = 0,  // no args: start a patch (.hcd) upload
	kBtMethodUploadChunk = 1,  // scalar in: offset; struct in: <= kBtChunkMax bytes
	kBtMethodUp          = 2,  // no args: power, patch, baud switch, setup (blocks ~5 s)
	kBtMethodCommand     = 3,  // struct in: bt_command; struct out: bt_packet (the matching event)
	kBtMethodReadEvent   = 4,  // scalar in: timeout ms; struct out: bt_packet
	kBtMethodStatus      = 5,  // struct out: bt_status (no HCI traffic; not root-only)
	kBtMethodCount
};

#define kBtChunkMax    4096
#define kBtPatchMax    (64 * 1024)
#define kBtPacketMax   1028        // ACL header (4) + 1021, or an event (2 + 255)

enum {
	kBtStateDown    = 0,  // driver started, controller not powered
	kBtStateLoading = 1,  // patch upload / bring-up in progress
	kBtStateUp      = 2,  // patched, at the operating speed, event masks set
	kBtStateFailed  = 3,
};

// H4 packet types (Bluetooth Core spec, UART transport layer).
enum {
	kBtH4Command = 0x01,
	kBtH4Acl     = 0x02,
	kBtH4Event   = 0x04,
};

typedef struct {
	uint16_t opcode;           // OGF << 10 | OCF
	uint8_t  plen;
	uint8_t  reserved;
	uint8_t  params[255];
	uint8_t  pad;
} bt_command;

typedef struct {
	uint8_t  type;             // kBtH4Event or kBtH4Acl
	uint8_t  reserved;
	uint16_t len;              // bytes in data: the packet after the H4 type byte
	uint8_t  data[kBtPacketMax];
} bt_packet;

typedef struct {
	uint32_t state;
	uint32_t baud;
	uint8_t  bdaddr[6];        // display order (most significant byte first)
	uint16_t hci_revision;
	uint16_t lmp_subversion;
	uint8_t  hci_version;
	uint8_t  lmp_version;
	char     name[64];         // Read_Local_Name after the patch
	uint64_t rx_bytes;
	uint64_t tx_bytes;
	uint32_t rx_overruns;      // PL011 OE: bytes lost in hardware
	uint32_t rx_errors;        // PL011 FE/PE/BE
	uint32_t rx_ring_drops;    // bytes lost because the driver's ring was full
	uint32_t rx_resyncs;       // H4 bytes skipped to find the next packet
	uint32_t event_drops;      // packets dropped because the event queue was full
	uint32_t reserved;
	char     lasterror[96];
} bt_status;

#endif /* _BCM43430BLUETOOTHSHARED_H */
