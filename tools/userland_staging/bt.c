/*
 * bt - control the Pi 3's on-board Bluetooth controller through
 * BCM43430BluetoothUserClient (docs/bluetooth-plan.md; ABI in
 * iokit/BCM43430Bluetooth/BCM43430BluetoothShared.h).
 *
 *   bt up [file]          load the patch (default /usr/share/firmware/bluetooth/BCM43430A1.hcd)
 *   bt info               state, address, versions, counters
 *   bt scan [secs]        classic inquiry + LE scan (default 10 s)
 *   bt hci <opcode> [hex bytes...]
 *                         send one HCI command, print the response event;
 *                         opcode as 0x1001 or ogf.ocf (4.1)
 *
 * The kernel needs boot-arg bt=1. Raw HCI only: no L2CAP/pairing yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "BCM43430BluetoothShared.h"

#define BT_FW "/usr/share/firmware/bluetooth/BCM43430A1.hcd"

static io_connect_t conn;

static const char *
errstr(kern_return_t kr)
{
	static char b[32];
	switch (kr) {
	case kIOReturnNotReady:      return "not ready (run `bt up`)";
	case kIOReturnNotPrivileged: return "must be root";
	case kIOReturnNotPermitted:  return "opcode is reserved for the driver";
	case kIOReturnTimeout:       return "timed out";
	case kIOReturnBusy:          return "busy";
	case kIOReturnBadArgument:   return "bad argument";
	case kIOReturnExclusiveAccess: return "another bt process is reading events";
	case kIOReturnUnsupported:   return "unsupported";
	}
	snprintf(b, sizeof(b), "0x%x", kr);
	return b;
}

static int
getStatus(bt_status *st)
{
	size_t sz = sizeof(*st);
	return IOConnectCallStructMethod(conn, kBtMethodStatus, NULL, 0, st, &sz) == KERN_SUCCESS ? 0 : -1;
}

static void
lasterror(void)
{
	bt_status st;
	if (getStatus(&st) == 0 && st.lasterror[0]) {
		fprintf(stderr, "bt: driver says: %s\n", st.lasterror);
	}
}

static int
openDriver(void)
{
	io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching(kBCM43430BluetoothClass));
	if (svc == IO_OBJECT_NULL) {
		fprintf(stderr, "bt: no %s service (boot with bt=1?)\n", kBCM43430BluetoothClass);
		return -1;
	}
	kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
	IOObjectRelease(svc);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "bt: IOServiceOpen: %s\n", errstr(kr));
		return -1;
	}
	return 0;
}

static int
cmdUp(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "bt: cannot open %s\n", path);
		return 1;
	}
	struct stat sb;
	if (fstat(fd, &sb) != 0 || sb.st_size <= 0 || sb.st_size > kBtPatchMax) {
		fprintf(stderr, "bt: %s: bad size\n", path);
		close(fd);
		return 1;
	}
	uint8_t *buf = malloc((size_t)sb.st_size);
	size_t got = 0;
	while (buf && got < (size_t)sb.st_size) {
		ssize_t n = read(fd, buf + got, (size_t)sb.st_size - got);
		if (n <= 0) {
			break;
		}
		got += (size_t)n;
	}
	close(fd);
	if (buf == NULL || got != (size_t)sb.st_size) {
		fprintf(stderr, "bt: cannot read %s\n", path);
		return 1;
	}
	kern_return_t kr = IOConnectCallScalarMethod(conn, kBtMethodUploadBegin, NULL, 0, NULL, NULL);
	for (size_t off = 0; kr == KERN_SUCCESS && off < got; off += kBtChunkMax) {
		size_t n = got - off > kBtChunkMax ? kBtChunkMax : got - off;
		uint64_t in[1] = { off };
		kr = IOConnectCallMethod(conn, kBtMethodUploadChunk, in, 1, buf + off, n, NULL, NULL, NULL, NULL);
	}
	free(buf);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "bt: patch upload: %s\n", errstr(kr));
		lasterror();
		return 1;
	}
	printf("bt: loading patch (%zu bytes)\n", got);
	kr = IOConnectCallScalarMethod(conn, kBtMethodUp, NULL, 0, NULL, NULL);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "bt: up: %s\n", errstr(kr));
		lasterror();
		return 1;
	}
	bt_status st;
	if (getStatus(&st) == 0) {
		printf("bt: up, %02x:%02x:%02x:%02x:%02x:%02x \"%s\" at %u baud\n",
		    st.bdaddr[0], st.bdaddr[1], st.bdaddr[2], st.bdaddr[3], st.bdaddr[4], st.bdaddr[5],
		    st.name, st.baud);
	}
	return 0;
}

static int
cmdInfo(void)
{
	static const char *states[] = { "down", "loading", "up", "failed" };
	bt_status st;
	if (getStatus(&st) != 0) {
		fprintf(stderr, "bt: status failed\n");
		return 1;
	}
	printf("state:      %s\n", st.state < 4 ? states[st.state] : "?");
	if (st.state == kBtStateUp) {
		printf("address:    %02x:%02x:%02x:%02x:%02x:%02x\n",
		    st.bdaddr[0], st.bdaddr[1], st.bdaddr[2], st.bdaddr[3], st.bdaddr[4], st.bdaddr[5]);
		printf("name:       %s\n", st.name);
		printf("hci:        %u rev %#x\n", st.hci_version, st.hci_revision);
		printf("lmp:        %u subversion %#x\n", st.lmp_version, st.lmp_subversion);
		printf("baud:       %u\n", st.baud);
	}
	printf("rx/tx:      %llu / %llu bytes\n", (unsigned long long)st.rx_bytes, (unsigned long long)st.tx_bytes);
	printf("errors:     overrun %u, framing/parity %u, ring drops %u, resyncs %u, event drops %u\n",
	    st.rx_overruns, st.rx_errors, st.rx_ring_drops, st.rx_resyncs, st.event_drops);
	if (st.lasterror[0]) {
		printf("last error: %s\n", st.lasterror);
	}
	return 0;
}

static int
hci(uint16_t opcode, const uint8_t *p, uint8_t plen, bt_packet *resp, int quiet)
{
	bt_command c;
	memset(&c, 0, sizeof(c));
	c.opcode = opcode;
	c.plen = plen;
	if (plen) {
		memcpy(c.params, p, plen);
	}
	size_t sz = sizeof(*resp);
	kern_return_t kr = IOConnectCallStructMethod(conn, kBtMethodCommand, &c, sizeof(c), resp, &sz);
	if (kr != KERN_SUCCESS) {
		if (!quiet) {
			fprintf(stderr, "bt: HCI %#06x: %s\n", opcode, errstr(kr));
		}
		return -1;
	}
	// Status: Command Complete byte 5, Command Status byte 2.
	uint8_t status = resp->data[0] == 0x0e ? (resp->len > 5 ? resp->data[5] : 0xff) : resp->data[2];
	if (status != 0 && !quiet) {
		fprintf(stderr, "bt: HCI %#06x: status %#x\n", opcode, status);
	}
	return status == 0 ? 0 : -1;
}

// ---------------------------------------------------------------- scan

struct dev {
	uint8_t addr[6];
	int le;
	int rssi;
	char name[64];
};
static struct dev devs[128];
static int ndevs;

static struct dev *
devFor(const uint8_t *addrLE, int le)
{
	uint8_t a[6];
	for (int i = 0; i < 6; i++) {
		a[i] = addrLE[5 - i];
	}
	for (int i = 0; i < ndevs; i++) {
		if (memcmp(devs[i].addr, a, 6) == 0 && devs[i].le == le) {
			return &devs[i];
		}
	}
	if (ndevs == (int)(sizeof(devs) / sizeof(devs[0]))) {
		return NULL;
	}
	struct dev *d = &devs[ndevs++];
	memset(d, 0, sizeof(*d));
	memcpy(d->addr, a, 6);
	d->le = le;
	d->rssi = 127;
	return d;
}

// EIR and LE advertising data share the {len, type, data} structure; names
// are type 0x09 (complete) or 0x08 (shortened).
static void
parseName(struct dev *d, const uint8_t *p, int len)
{
	for (int i = 0; i + 1 < len; ) {
		int l = p[i];
		if (l == 0 || i + 1 + l > len) {
			break;
		}
		int type = p[i + 1];
		if ((type == 0x09 || (type == 0x08 && !d->name[0])) && l > 1) {
			int n = l - 1 < (int)sizeof(d->name) - 1 ? l - 1 : (int)sizeof(d->name) - 1;
			memcpy(d->name, p + i + 2, n);
			d->name[n] = 0;
		}
		i += 1 + l;
	}
}

static int
handleEvent(const bt_packet *e)
{
	if (e->type != kBtH4Event || e->len < 2) {
		return 0;
	}
	const uint8_t *p = e->data + 2;
	int plen = e->len - 2;
	switch (e->data[0]) {
	case 0x01:   // Inquiry Complete
		return 1;
	case 0x22: { // Inquiry Result with RSSI: num, then 14-byte entries
		int n = plen > 0 ? p[0] : 0;
		for (int i = 0; i < n && 1 + (i + 1) * 14 <= plen; i++) {
			const uint8_t *r = p + 1 + i * 14;
			struct dev *d = devFor(r, 0);
			if (d) d->rssi = (int8_t)r[13];
		}
		break;
	}
	case 0x2f: { // Extended Inquiry Result: num(1), addr, psrm, res, cod[3], clock[2], rssi, eir[240]
		if (plen >= 15) {
			struct dev *d = devFor(p + 1, 0);
			if (d) {
				d->rssi = (int8_t)p[14];
				parseName(d, p + 15, plen - 15);
			}
		}
		break;
	}
	case 0x02: { // Inquiry Result (no RSSI): num, then field arrays
		int n = plen > 0 ? p[0] : 0;
		for (int i = 0; i < n && 1 + (i + 1) * 6 <= plen; i++) {
			devFor(p + 1 + i * 6, 0);
		}
		break;
	}
	case 0x3e: { // LE Meta
		if (plen >= 2 && p[0] == 0x02) {    // LE Advertising Report
			int n = p[1], off = 2;
			for (int i = 0; i < n && off + 9 <= plen; i++) {
				const uint8_t *r = p + off;   // evt_type, addr_type, addr[6], len, data, rssi
				int dlen = r[8];
				if (off + 9 + dlen + 1 > plen) {
					break;
				}
				struct dev *d = devFor(r + 2, 1);
				if (d) {
					d->rssi = (int8_t)r[9 + dlen];
					parseName(d, r + 9, dlen);
				}
				off += 9 + dlen + 1;
			}
		}
		break;
	}
	}
	return 0;
}

static double
now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static int
cmdScan(int secs)
{
	bt_packet resp;
	// The driver queues events only once a reader has claimed the queue;
	// claim it (a zero-timeout read) before anything starts reporting.
	{
		bt_packet e;
		size_t sz = sizeof(e);
		uint64_t to[1] = { 0 };
		kern_return_t kr = IOConnectCallMethod(conn, kBtMethodReadEvent, to, 1, NULL, 0, NULL, NULL, &e, &sz);
		if (kr != KERN_SUCCESS && kr != kIOReturnTimeout) {
			fprintf(stderr, "bt: %s\n", errstr(kr));
			return 1;
		}
	}
	uint8_t mode = 0x02;                       // Write_Inquiry_Mode: extended
	hci(0x0c45, &mode, 1, &resp, 0);
	// LE_Set_Scan_Parameters: active (names often come in scan responses),
	// interval = window = 0x0010 (10 ms), public address, accept all.
	uint8_t sp[7] = { 0x01, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00 };
	int le = hci(0x200b, sp, sizeof(sp), &resp, 0) == 0;
	uint8_t se[2] = { 0x01, 0x01 };            // enable, filter duplicates
	le = le && hci(0x200c, se, sizeof(se), &resp, 0) == 0;
	// HCI_Inquiry: GIAC 0x9e8b33, length in 1.28 s units, unlimited responses.
	int units = (int)(secs / 1.28);
	if (units < 1) units = 1;
	if (units > 0x30) units = 0x30;
	uint8_t inq[5] = { 0x33, 0x8b, 0x9e, (uint8_t)units, 0x00 };
	int classic = hci(0x0401, inq, sizeof(inq), &resp, 0) == 0;
	if (!le && !classic) {
		return 1;
	}
	printf("bt: scanning %d s (%s%s%s)\n", secs, classic ? "classic" : "", classic && le ? " + " : "", le ? "LE" : "");

	double end = now() + secs;
	int inquiryDone = !classic;
	while (now() < end) {
		bt_packet e;
		size_t sz = sizeof(e);
		uint64_t to[1] = { 500 };
		kern_return_t kr = IOConnectCallMethod(conn, kBtMethodReadEvent, to, 1, NULL, 0, NULL, NULL, &e, &sz);
		if (kr == kIOReturnTimeout) {
			continue;
		}
		if (kr != KERN_SUCCESS) {
			fprintf(stderr, "bt: read event: %s\n", errstr(kr));
			break;
		}
		if (handleEvent(&e)) {
			inquiryDone = 1;
		}
	}
	if (le) {
		uint8_t off[2] = { 0, 0 };
		hci(0x200c, off, sizeof(off), &resp, 1);
	}
	if (!inquiryDone) {
		hci(0x0402, NULL, 0, &resp, 1);        // Inquiry_Cancel
	}

	printf("%-17s  %-7s  %5s  %s\n", "ADDRESS", "TYPE", "RSSI", "NAME");
	for (int i = 0; i < ndevs; i++) {
		struct dev *d = &devs[i];
		char rssi[8] = "?";
		if (d->rssi != 127) {
			snprintf(rssi, sizeof(rssi), "%d", d->rssi);
		}
		printf("%02x:%02x:%02x:%02x:%02x:%02x  %-7s  %5s  %s\n",
		    d->addr[0], d->addr[1], d->addr[2], d->addr[3], d->addr[4], d->addr[5],
		    d->le ? "LE" : "classic", rssi, d->name);
	}
	printf("bt: %d device(s)\n", ndevs);
	return 0;
}

static int
cmdHci(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "usage: bt hci <opcode|ogf.ocf> [hex bytes...]\n");
		return 2;
	}
	unsigned op;
	char *dot = strchr(argv[0], '.');
	if (dot) {
		op = (unsigned)(strtoul(argv[0], NULL, 0) << 10 | strtoul(dot + 1, NULL, 0));
	} else {
		op = (unsigned)strtoul(argv[0], NULL, 0);
	}
	uint8_t p[255];
	int n = 0;
	for (int i = 1; i < argc && n < 255; i++) {
		p[n++] = (uint8_t)strtoul(argv[i], NULL, 16);
	}
	bt_packet resp;
	memset(&resp, 0, sizeof(resp));     // untouched when the call itself fails
	int rc = hci((uint16_t)op, p, (uint8_t)n, &resp, 0);
	if (resp.len > 0) {
		printf("event %#04x:", resp.data[0]);
		for (int i = 2; i < resp.len; i++) {
			printf(" %02x", resp.data[i]);
		}
		printf("\n");
	}
	return rc == 0 ? 0 : 1;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: bt up [file] | info | scan [secs] | hci <opcode> [bytes...]\n");
		return 2;
	}
	if (openDriver() != 0) {
		return 1;
	}
	const char *c = argv[1];
	int rc;
	if (strcmp(c, "up") == 0) {
		rc = cmdUp(argc > 2 ? argv[2] : BT_FW);
	} else if (strcmp(c, "info") == 0) {
		rc = cmdInfo();
	} else if (strcmp(c, "scan") == 0) {
		int secs = argc > 2 ? atoi(argv[2]) : 10;
		rc = cmdScan(secs > 0 && secs <= 60 ? secs : 10);
	} else if (strcmp(c, "hci") == 0) {
		rc = cmdHci(argc - 2, argv + 2);
	} else {
		fprintf(stderr, "bt: unknown command %s\n", c);
		rc = 2;
	}
	IOServiceClose(conn);
	return rc;
}
