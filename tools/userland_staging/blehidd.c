/*
 * blehidd -- Bluetooth LE HID host for this port (docs/bluetooth-stack-plan.md
 * S8). Pairs with LE keyboards and mice, reconnects them, and presents each
 * one to IOKit as an IOHIDUserDevice -- the same path bthidd(8) gives classic
 * Bluetooth HID devices, and USB HID devices take, to X.
 *
 * netbt has no LE protocols. Our fork lets a root raw HCI socket own LE
 * links (sys/netbt hci_le_output and friends): netbt notes each LE handle
 * from LE Connection Complete, keeps its ACL buffer credits, and lets this
 * socket send its ACL data. Everything above HCI is done here:
 *
 *   connect    LE Create Connection (Core 5.x Vol 4 Part E 7.8.12), to the
 *              device being paired, or with the White List (7.8.15-16) to
 *              every bonded device, so whichever advertises connects.
 *   L2CAP      LE fixed channels: 4 ATT, 5 LE signalling, 6 SMP (Vol 3
 *              Part A 2.1). Signalling answers Connection Parameter Update
 *              Request (4.20/4.21) and applies it (LE Connection Update).
 *   SMP        LE legacy pairing, Just Works, as initiator (Vol 3 Part H
 *              2.3.5.2, 3.5): Pairing Request/Response, Confirm, Random,
 *              c1 (2.2.3), s1 (2.2.4) through HCI LE Encrypt (Vol 4 Part E
 *              7.8.22), the STK by LE Enable Encryption (7.8.24), then the
 *              responder's LTK, EDIV and Rand (3.6). Byte order as BlueZ
 *              src/shared/crypto.c bt_crypto_c1/s1: every value
 *              little-endian, as on the air and in the HCI command.
 *   reconnect  LE Enable Encryption with the stored LTK, EDIV and Rand.
 *   GATT       Vol 3 Part G 4.4.1 (Read By Group Type for primary services),
 *              4.6.1 (Read By Type for characteristics), 4.7.1 (Find
 *              Information for descriptors), 4.8.1/4.8.3 (Read, Read Blob),
 *              4.9.3 (Write Request), 4.10 (notifications), 4.11
 *              (indications). ATT_MTU stays 23.
 *   HOGP       HID over GATT Profile 1.0: the HID service (0x1812), its
 *              Report Map (0x2A4B), each Report (0x2A4D) with its Report
 *              Reference (0x2908: id, type); input reports get their CCCD
 *              (0x2902) set to notify. A notification is one input report
 *              without its id, which goes in front again when the Report
 *              Map uses ids (HOGP 4.4). PnP ID (0x2A50) gives vendor and
 *              product.
 *
 * Files (btsetup(8) uses them):
 *   /var/db/blehidd.db       one bonded device per line:
 *                            addr type ltk ediv rand keysize
 *   /var/run/blehidd.pair    "addr type": pair with it (then SIGHUP)
 *   /var/run/blehidd.status  "addr state" per device: connected,
 *                            connecting, disconnected, failed
 * SIGHUP rereads both (a device removed from the db is disconnected).
 *
 * Usage: blehidd [-d]     (-d: log to stderr too)
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <bluetooth.h>
#include <netbt/hci_le.h>

#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFSerialize.h>
#include <CoreFoundation/CoreFoundation.h>

#define UNIT            "bcmbt0"
#define DBFILE          "/var/db/blehidd.db"
#define PAIRFILE        "/var/run/blehidd.pair"
#define STATUSFILE      "/var/run/blehidd.status"
#define PIDFILE         "/var/run/blehidd.pid"
#define MAXDEV          8
#define MAXIN           16
#define ATT_MTU         23
#define ATT_TIMEOUT_MS  30000           /* Vol 3 Part F 3.3.3 */
#define SMP_TIMEOUT_MS  30000           /* Vol 3 Part H 3.4 */
#define PAIR_GIVEUP     90              /* s without a connection */

#define CID_ATT         0x0004
#define CID_LESIG       0x0005
#define CID_SMP         0x0006

enum { kResourceTypeDevice = 0 };                       /* IOHIDResourceUserClient.h */
enum { kMethodCreate = 0, kMethodTerminate = 1, kMethodHandleReport = 2 };

struct ledev {
	bool            used, keep;
	bdaddr_t        addr;
	uint8_t         atype;          /* 0 public, 1 random */
	bool            bonded;
	uint8_t         ltk[16], rand[8];
	uint16_t        ediv;
	uint8_t         keysize;
	bool            pairing, failed;
	time_t          pair_since;
	/* connection */
	int             handle;         /* -1 none */
	bool            setup;          /* connected, set-up still to do */
	bool            ready;          /* reports flowing */
	uint8_t         rx[1024];       /* L2CAP reassembly */
	size_t          rxlen, rxwant;
	io_connect_t    hid;
	uint8_t        *map;
	size_t          maplen;
	struct { uint16_t vh; uint8_t id; } in[MAXIN];
	int             nin;
	bool            ids;
	uint16_t        vendor, product;
};

static struct ledev dev[MAXDEV];
static int hs = -1;                     /* raw HCI socket */
static bdaddr_t local;
static int sigpipe[2];
static bool debug;
static bool connecting;                 /* an LE Create Connection is pending */
static char connecting_for[MAXDEV * 20 + 1];
static bool unit_up;

/* ---------------------------------------------------------------- misc */

static void
logmsg(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
	if (debug) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
		va_end(ap);
	}
}

static const char *
addr(const bdaddr_t *b)
{
	static char buf[4][20];
	static int n;
	n = (n + 1) % 4;
	return bt_ntoa(b, buf[n]);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static long
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void
hex(char *out, const uint8_t *p, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		sprintf(out + 2 * i, "%02x", p[i]);
}

static bool
unhex(uint8_t *out, const char *s, size_t n)
{
	size_t i;
	unsigned v;
	if (strlen(s) != 2 * n)
		return false;
	for (i = 0; i < n; i++) {
		if (sscanf(s + 2 * i, "%2x", &v) != 1)
			return false;
		out[i] = (uint8_t)v;
	}
	return true;
}

static struct ledev *
find_addr(const bdaddr_t *a)
{
	int i;
	for (i = 0; i < MAXDEV; i++)
		if (dev[i].used && bdaddr_same(&dev[i].addr, a))
			return &dev[i];
	return NULL;
}

static struct ledev *
find_handle(int h)
{
	int i;
	for (i = 0; i < MAXDEV; i++)
		if (dev[i].used && dev[i].handle == h)
			return &dev[i];
	return NULL;
}

/* ---------------------------------------------------------------- status and db */

static void
write_status(void)
{
	static char last[MAXDEV * 40 + 1];
	static bool written;
	char cur[sizeof(last)];
	size_t len = 0;
	FILE *f;
	int i;

	cur[0] = 0;
	for (i = 0; i < MAXDEV; i++) {
		struct ledev *d = &dev[i];
		const char *st;
		if (!d->used)
			continue;
		st = d->ready ? "connected" : d->failed ? "failed"
		    : (d->handle >= 0 || d->pairing) ? "connecting" : "disconnected";
		len += (size_t)snprintf(cur + len, sizeof(cur) - len, "%s %s\n", addr(&d->addr), st);
		if (len >= sizeof(cur))
			break;
	}
	if (written && strcmp(cur, last) == 0)
		return;
	if ((f = fopen(STATUSFILE ".new", "w")) == NULL)
		return;
	fputs(cur, f);
	if (fclose(f) == 0 && rename(STATUSFILE ".new", STATUSFILE) == 0) {
		strlcpy(last, cur, sizeof(last));
		written = true;
	}
}

static void
save_db(void)
{
	FILE *f;
	char ltk[33], rnd[17];
	int i;

	if ((f = fopen(DBFILE ".new", "w")) == NULL) {
		logmsg(LOG_ERR, "%s: %s", DBFILE, strerror(errno));
		return;
	}
	(void)fchmod(fileno(f), 0600);
	for (i = 0; i < MAXDEV; i++) {
		struct ledev *d = &dev[i];
		if (!d->used || !d->bonded)
			continue;
		hex(ltk, d->ltk, 16);
		hex(rnd, d->rand, 8);
		fprintf(f, "%s %u %s %04x %s %u\n", addr(&d->addr), d->atype, ltk,
		    d->ediv, rnd, d->keysize);
	}
	if (fclose(f) == 0)
		(void)rename(DBFILE ".new", DBFILE);
}

static struct ledev *
dev_add(const bdaddr_t *a, uint8_t atype)
{
	struct ledev *d = find_addr(a);
	int i;

	if (d != NULL)
		return d;
	for (i = 0; i < MAXDEV; i++) {
		if (!dev[i].used) {
			d = &dev[i];
			memset(d, 0, sizeof(*d));
			d->used = true;
			bdaddr_copy(&d->addr, a);
			d->atype = atype;
			d->handle = -1;
			return d;
		}
	}
	return NULL;
}

static void dev_disconnect(struct ledev *, const char *);
static void dev_lost(struct ledev *);

/* Reread the db and the pair request; drop devices no longer in either. */
static void
load_config(void)
{
	FILE *f;
	char line[256], a[32], ltk[64], rnd[32];
	unsigned type, ediv, ks;
	bdaddr_t ba;
	struct ledev *d;
	int i;

	for (i = 0; i < MAXDEV; i++)
		dev[i].keep = dev[i].pairing && !dev[i].failed;
	if ((f = fopen(DBFILE, "r")) != NULL) {
		while (fgets(line, sizeof(line), f) != NULL) {
			if (sscanf(line, "%31s %u %63s %x %31s %u", a, &type, ltk, &ediv, rnd, &ks) != 6
			    || !bt_aton(a, &ba))
				continue;
			if ((d = dev_add(&ba, (uint8_t)type)) == NULL)
				continue;
			d->keep = true;
			if (!d->pairing && unhex(d->ltk, ltk, 16) && unhex(d->rand, rnd, 8)) {
				d->bonded = true;
				d->ediv = (uint16_t)ediv;
				d->keysize = (uint8_t)ks;
			}
		}
		fclose(f);
	}
	if ((f = fopen(PAIRFILE, "r")) != NULL) {
		if (fgets(line, sizeof(line), f) != NULL
		    && sscanf(line, "%31s %u", a, &type) == 2 && bt_aton(a, &ba)
		    && (d = dev_add(&ba, (uint8_t)type)) != NULL) {
			logmsg(LOG_NOTICE, "%s: pairing requested", addr(&ba));
			if (d->handle >= 0)
				dev_disconnect(d, "pairing again");
			d->keep = true;
			d->pairing = true;
			d->failed = false;
			d->bonded = false;
			d->atype = (uint8_t)type;
			d->pair_since = time(NULL);
		}
		fclose(f);
		unlink(PAIRFILE);
	}
	for (i = 0; i < MAXDEV; i++) {
		d = &dev[i];
		if (d->used && !d->keep) {
			logmsg(LOG_NOTICE, "%s: forgotten", addr(&d->addr));
			if (d->handle >= 0)
				dev_disconnect(d, "forgotten");
			dev_lost(d);
			d->used = false;
		}
	}
}

/* ---------------------------------------------------------------- IOKit */

static void
cfset(CFMutableDictionaryRef d, const char *key, CFTypeRef v)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	CFDictionarySetValue(d, k, v);
	CFRelease(k);
	CFRelease(v);
}

static CFNumberRef cfnum(int v) { return CFNumberCreate(NULL, kCFNumberIntType, &v); }
static CFStringRef cfstr(const char *s) { return CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8); }

/* as bthidd.c hid_create (the sequence of hid_userdevice_test.c, H0) */
static io_connect_t
hid_create(struct ledev *d)
{
	io_service_t res;
	io_connect_t conn = MACH_PORT_NULL;
	CFMutableDictionaryRef p;
	CFDataRef ser;
	char name[64];
	uint64_t options = 0;
	kern_return_t kr = KERN_FAILURE;

	res = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOHIDResource"));
	if (res == MACH_PORT_NULL) {
		logmsg(LOG_ERR, "no IOHIDResource service");
		return MACH_PORT_NULL;
	}
	p = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	cfset(p, "ReportDescriptor", CFDataCreate(NULL, d->map, (CFIndex)d->maplen));
	cfset(p, "VendorID", cfnum(d->vendor));
	cfset(p, "ProductID", cfnum(d->product));
	snprintf(name, sizeof(name), "Bluetooth LE HID %s", addr(&d->addr));
	cfset(p, "Product", cfstr(name));
	cfset(p, "SerialNumber", cfstr(addr(&d->addr)));
	cfset(p, "Transport", cfstr("Bluetooth Low Energy"));
	ser = IOCFSerialize(p, 0);
	CFRelease(p);
	if (ser == NULL) {
		IOObjectRelease(res);
		return MACH_PORT_NULL;
	}
	kr = IOServiceOpen(res, mach_task_self(), kResourceTypeDevice, &conn);
	IOObjectRelease(res);
	if (kr == KERN_SUCCESS) {
		kr = IOConnectCallMethod(conn, kMethodCreate, &options, 1,
		    CFDataGetBytePtr(ser), (size_t)CFDataGetLength(ser), NULL, NULL, NULL, NULL);
		if (kr != KERN_SUCCESS) {
			IOServiceClose(conn);
			conn = MACH_PORT_NULL;
		}
	}
	CFRelease(ser);
	if (conn == MACH_PORT_NULL)
		logmsg(LOG_ERR, "%s: IOHIDUserDevice create failed (0x%x)", addr(&d->addr), kr);
	else
		logmsg(LOG_NOTICE, "%s: HID device up (vendor 0x%04x product 0x%04x, %zu-byte report map, %d input report%s)",
		    addr(&d->addr), d->vendor, d->product, d->maplen, d->nin, d->nin == 1 ? "" : "s");
	return conn;
}

static void
hid_destroy(struct ledev *d)
{
	if (d->hid == MACH_PORT_NULL)
		return;
	(void)IOConnectCallMethod(d->hid, kMethodTerminate, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
	IOServiceClose(d->hid);
	d->hid = MACH_PORT_NULL;
	logmsg(LOG_NOTICE, "%s: HID device down", addr(&d->addr));
}

static void
hid_report(struct ledev *d, const uint8_t *r, size_t len)
{
	uint64_t ts = 0;        /* 0: the kernel timestamps it */

	if (d->hid == MACH_PORT_NULL || len == 0)
		return;
	(void)IOConnectCallMethod(d->hid, kMethodHandleReport, &ts, 1, r, len, NULL, NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- HCI */

static int
hci_cmd_send(uint16_t opcode, const void *p, size_t len)
{
	if (bt_devsend(hs, opcode, (void *)(uintptr_t)p, len) < 0) {
		logmsg(LOG_ERR, "command %04x: %s", opcode, strerror(errno));
		return -1;
	}
	return 0;
}

static int
l2cap_send(struct ledev *d, uint16_t cid, const uint8_t *p, size_t len)
{
	uint8_t b[5 + 4 + 64];

	if (d->handle < 0 || len > sizeof(b) - 9)
		return -1;
	b[0] = HCI_ACL_DATA_PKT;
	put16(b + 1, (uint16_t)(d->handle & 0x0fff));  /* PB 00: first, not flushable (LE) */
	put16(b + 3, (uint16_t)(4 + len));
	put16(b + 5, (uint16_t)len);
	put16(b + 7, cid);
	memcpy(b + 9, p, len);
	/*
	 * netbt notes an LE handle (hci_event_le_meta) just after hci_mtap()
	 * has handed us the LE Connection Complete, so right after a connect
	 * the handle can still be unknown for a moment: ENOTCONN, try again.
	 */
	for (int tries = 0; send(hs, b, 9 + len, 0) < 0; tries++) {
		if (errno != ENOTCONN || tries == 10) {
			logmsg(LOG_ERR, "%s: ACL send: %s", addr(&d->addr), strerror(errno));
			return -1;
		}
		usleep(20000);
	}
	return 0;
}

/*
 * What came in: an event, or a whole L2CAP frame on an LE link.
 */
struct item {
	enum { IT_NONE, IT_EVENT, IT_L2CAP } type;
	uint8_t         code;           /* event code */
	const uint8_t  *p;              /* event params / L2CAP payload */
	size_t          len;
	struct ledev   *d;
	uint16_t        cid;
};

static uint8_t rbuf[2048];

/* One packet from the socket, incoming only; false on timeout or error. */
static bool
read_packet(size_t *lenp, int timeout_ms)
{
	struct pollfd pfd = { hs, POLLIN, 0 };
	long end = now_ms() + timeout_ms;
	struct msghdr msg;
	struct iovec iov = { rbuf, sizeof(rbuf) };
	union { struct cmsghdr h; uint8_t b[64]; } ctl;
	struct cmsghdr *c;
	ssize_t n;
	int dir;

	for (;;) {
		long left = end - now_ms();
		if (poll(&pfd, 1, left > 0 ? (int)left : 0) <= 0)
			return false;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ctl.b;
		msg.msg_controllen = sizeof(ctl.b);
		n = recvmsg(hs, &msg, 0);
		if (n <= 0)
			return false;
		dir = 1;
		for (c = CMSG_FIRSTHDR(&msg); c != NULL; c = CMSG_NXTHDR(&msg, c))
			if (c->cmsg_level == BTPROTO_HCI && c->cmsg_type == SCM_HCI_DIRECTION)
				memcpy(&dir, CMSG_DATA(c), sizeof(dir));
		if (dir == 0)           /* our own ACL coming back: skip it */
			continue;
		*lenp = (size_t)n;
		return true;
	}
}

/* Next item, or IT_NONE after timeout_ms. */
static void
next_item(struct item *it, int timeout_ms)
{
	long end = now_ms() + timeout_ms;
	size_t n;

	it->type = IT_NONE;
	for (;;) {
		long left = end - now_ms();
		if (left < 0)
			left = 0;
		if (!read_packet(&n, (int)left))
			return;
		if (rbuf[0] == HCI_EVENT_PKT && n >= 3 && n >= 3u + rbuf[2]) {
			it->type = IT_EVENT;
			it->code = rbuf[1];
			it->p = rbuf + 3;
			it->len = rbuf[2];
			return;
		}
		if (rbuf[0] == HCI_ACL_DATA_PKT && n >= 5) {
			uint16_t hf = le16(rbuf + 1), dl = le16(rbuf + 3);
			struct ledev *d = find_handle(hf & 0x0fff);
			int pb = (hf >> 12) & 3;
			if (d == NULL || n < 5u + dl)
				continue;
			if (pb != 1) {          /* start of a frame */
				if (dl < 4)
					continue;
				d->rxlen = 0;
				d->rxwant = 4u + le16(rbuf + 5);
			} else if (d->rxwant == 0)
				continue;
			if (d->rxlen + dl > sizeof(d->rx)) {
				d->rxwant = 0;
				continue;
			}
			memcpy(d->rx + d->rxlen, rbuf + 5, dl);
			d->rxlen += dl;
			if (d->rxlen < d->rxwant)
				continue;
			d->rxwant = 0;
			it->type = IT_L2CAP;
			it->d = d;
			it->cid = le16(d->rx + 2);
			it->p = d->rx + 4;
			it->len = d->rxlen - 4;
			return;
		}
		if (end - now_ms() <= 0)
			return;
	}
}

/* ---------------------------------------------------------------- asynchronous handling */

static void
dev_lost(struct ledev *d)
{
	hid_destroy(d);
	free(d->map);
	d->map = NULL;
	d->maplen = 0;
	d->nin = 0;
	d->handle = -1;
	d->setup = false;
	d->ready = false;
	d->rxwant = 0;
}

static void
dev_disconnect(struct ledev *d, const char *why)
{
	uint8_t cp[3];

	if (d->handle < 0)
		return;
	logmsg(LOG_INFO, "%s: disconnecting (%s)", addr(&d->addr), why);
	put16(cp, (uint16_t)d->handle);
	cp[2] = 0x13;   /* Remote User Terminated Connection */
	(void)hci_cmd_send(HCI_CMD_DISCONNECT, cp, sizeof(cp));
	/* Disconnection Complete does the rest; stop using the link now */
	hid_destroy(d);
	d->ready = false;
	d->setup = false;
}

/* The peripheral's own ATT requests: we have no attributes to offer. */
static void
att_serve(struct ledev *d, const uint8_t *p, size_t len)
{
	uint8_t r[5];

	switch (p[0]) {
	case 0x02:      /* Exchange MTU Request: ours is 23 */
		r[0] = 0x03;
		put16(r + 1, ATT_MTU);
		(void)l2cap_send(d, CID_ATT, r, 3);
		return;
	case 0x1d:      /* Handle Value Indication: confirm */
		r[0] = 0x1e;
		(void)l2cap_send(d, CID_ATT, r, 1);
		return;
	}
	if (p[0] & 0x40)        /* a command: no response (Vol 3 Part F 3.3.1) */
		return;
	if ((p[0] & 1) == 0) {  /* requests have even opcodes */
		r[0] = 0x01;
		r[1] = p[0];
		put16(r + 2, len >= 3 ? le16(p + 1) : 0);
		r[4] = (p[0] == 0x04 || p[0] == 0x06 || p[0] == 0x08 || p[0] == 0x10)
		    ? 0x0a      /* Attribute Not Found */
		    : 0x06;     /* Request Not Supported */
		(void)l2cap_send(d, CID_ATT, r, 5);
	}
}

static void
lesig(struct ledev *d, const uint8_t *p, size_t len)
{
	uint8_t r[16];

	if (len < 4)
		return;
	if (p[0] == 0x12 && len >= 12) {        /* Connection Parameter Update Request */
		uint8_t cu[14];
		r[0] = 0x13;
		r[1] = p[1];
		put16(r + 2, 2);
		put16(r + 4, 0);        /* accepted */
		(void)l2cap_send(d, CID_LESIG, r, 6);
		put16(cu, (uint16_t)d->handle);
		memcpy(cu + 2, p + 4, 8);       /* min, max, latency, timeout */
		put16(cu + 10, 0);
		put16(cu + 12, 0);
		(void)hci_cmd_send(0x2013, cu, sizeof(cu));     /* LE Connection Update */
		logmsg(LOG_INFO, "%s: connection interval %u-%u x1.25 ms, latency %u",
		    addr(&d->addr), le16(p + 4), le16(p + 6), le16(p + 8));
		return;
	}
	if ((p[0] & 1) == 0 && p[0] != 0x01) {  /* other requests: Command Reject */
		r[0] = 0x01;
		r[1] = p[1];
		put16(r + 2, 2);
		put16(r + 4, 0);        /* not understood */
		(void)l2cap_send(d, CID_LESIG, r, 6);
	}
}

static void
on_notify(struct ledev *d, const uint8_t *p, size_t len)
{
	uint8_t r[64];
	uint16_t h;
	int i;

	if (!d->ready || len < 3)
		return;
	h = le16(p + 1);
	for (i = 0; i < d->nin; i++) {
		if (d->in[i].vh != h)
			continue;
		if (d->ids) {
			if (len - 3 + 1 > sizeof(r))
				return;
			r[0] = d->in[i].id;
			memcpy(r + 1, p + 3, len - 3);
			hid_report(d, r, len - 3 + 1);
		} else
			hid_report(d, p + 3, len - 3);
		return;
	}
}

/*
 * Anything a synchronous wait did not want, and everything the main loop
 * reads. Never blocks.
 */
static void
handle_item(const struct item *it)
{
	struct ledev *d;

	if (it->type == IT_EVENT) {
		if (it->code == HCI_LE_META_EVENT && it->len >= 19 && it->p[0] == HCI_SUBEVT_CON_COMP) {
			const uint8_t *p = it->p;
			bdaddr_t ba;
			memcpy(&ba, p + 6, 6);
			connecting = false;
			connecting_for[0] = 0;
			if (p[1] != 0) {
				if (p[1] != 0x02)       /* 0x02: our own cancel */
					logmsg(LOG_INFO, "LE connection failed: status 0x%02x", p[1]);
				return;
			}
			d = find_addr(&ba);
			if (d == NULL) {
				uint8_t cp[3];
				put16(cp, le16(p + 2));
				cp[2] = 0x13;
				(void)hci_cmd_send(HCI_CMD_DISCONNECT, cp, sizeof(cp));
				return;
			}
			dev_lost(d);
			d->handle = le16(p + 2) & 0x0fff;
			d->setup = true;
			logmsg(LOG_NOTICE, "%s: connected (handle %d, interval %u x1.25 ms)",
			    addr(&d->addr), d->handle, le16(p + 12));
			return;
		}
		if (it->code == HCI_EVENT_DISCON_COMPL && it->len >= 4 && it->p[0] == 0) {
			d = find_handle(le16(it->p + 1) & 0x0fff);
			if (d != NULL) {
				logmsg(LOG_NOTICE, "%s: disconnected (reason 0x%02x)", addr(&d->addr), it->p[3]);
				dev_lost(d);
			}
			return;
		}
		return;
	}
	if (it->type != IT_L2CAP || it->len == 0)
		return;
	d = it->d;
	switch (it->cid) {
	case CID_ATT:
		if (it->p[0] == 0x1b)
			on_notify(d, it->p, it->len);
		else
			att_serve(d, it->p, it->len);
		break;
	case CID_LESIG:
		lesig(d, it->p, it->len);
		break;
	case CID_SMP:
		/* Security Request outside pairing: set-up encrypts anyway */
		break;
	}
}

/*
 * Wait for an item `want` accepts, handling everything else; false on
 * timeout or when d's link goes away meanwhile.
 */
typedef bool want_fn(const struct item *, const void *);

static bool
wait_for(struct item *it, want_fn *want, const void *arg, struct ledev *d, int timeout_ms)
{
	long end = now_ms() + timeout_ms;

	for (;;) {
		long left = end - now_ms();
		if (left <= 0)
			return false;
		next_item(it, (int)left);
		if (it->type == IT_NONE)
			return false;
		if (want(it, arg))
			return true;
		handle_item(it);
		if (d != NULL && d->handle < 0)
			return false;
	}
}

struct cmdwant { uint16_t opcode; };

static bool
want_cmd(const struct item *it, const void *arg)
{
	uint16_t op = ((const struct cmdwant *)arg)->opcode;

	if (it->type != IT_EVENT)
		return false;
	if (it->code == HCI_EVENT_COMMAND_COMPL && it->len >= 3 && le16(it->p + 1) == op)
		return true;
	if (it->code == HCI_EVENT_COMMAND_STATUS && it->len >= 4 && le16(it->p + 2) == op)
		return true;
	return false;
}

/*
 * A command and its Command Complete (return parameters to rp) or Command
 * Status. Returns the status, or -1.
 */
static int
hci_cmd(uint16_t opcode, const void *cp, size_t clen, uint8_t *rp, size_t rlen)
{
	struct cmdwant w = { opcode };
	struct item it;

	if (hci_cmd_send(opcode, cp, clen) < 0)
		return -1;
	if (!wait_for(&it, want_cmd, &w, NULL, 5000)) {
		logmsg(LOG_ERR, "command %04x: no answer", opcode);
		return -1;
	}
	if (it.code == HCI_EVENT_COMMAND_STATUS)
		return it.p[0];
	if (rp != NULL && rlen > 0) {
		size_t n = it.len - 3 < rlen ? it.len - 3 : rlen;
		memcpy(rp, it.p + 3, n);
	}
	return it.len > 3 ? it.p[3] : 0;
}

/* AES-128 by the controller: LE Encrypt, little-endian in and out (BlueZ bt_crypto_e) */
static bool
le_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	uint8_t cp[32], rp[17];

	memcpy(cp, key, 16);
	memcpy(cp + 16, in, 16);
	if (hci_cmd(0x2017, cp, sizeof(cp), rp, sizeof(rp)) != 0)
		return false;
	memcpy(out, rp + 1, 16);
	return true;
}

/* ---------------------------------------------------------------- ATT */

struct attwant { struct ledev *d; uint8_t req; };

static bool
want_att(const struct item *it, const void *arg)
{
	const struct attwant *w = arg;

	if (it->type != IT_L2CAP || it->d != w->d || it->cid != CID_ATT || it->len == 0)
		return false;
	if (it->p[0] == w->req + 1)
		return true;
	return it->p[0] == 0x01 && it->len >= 5 && it->p[1] == w->req;
}

/*
 * One ATT request and its response into rsp (whole PDU). Returns the PDU
 * length, -(error code) for an Error Response, or -0x100 when the link
 * went or nothing came.
 */
static int
att(struct ledev *d, const uint8_t *req, size_t len, uint8_t *rsp, size_t max)
{
	struct attwant w = { d, req[0] };
	struct item it;
	size_t n;

	if (l2cap_send(d, CID_ATT, req, len) < 0)
		return -0x100;
	if (!wait_for(&it, want_att, &w, d, ATT_TIMEOUT_MS))
		return -0x100;
	if (it.p[0] == 0x01)
		return -(int)it.p[4];
	n = it.len < max ? it.len : max;
	memcpy(rsp, it.p, n);
	return (int)n;
}

static int
att_read(struct ledev *d, uint16_t h, uint8_t *out, size_t max)
{
	uint8_t req[5], rsp[ATT_MTU];
	size_t got = 0;
	int n;

	req[0] = 0x0a;
	put16(req + 1, h);
	n = att(d, req, 3, rsp, sizeof(rsp));
	if (n < 1)
		return n < 0 ? n : -0x100;
	for (;;) {
		size_t k = (size_t)n - 1;
		if (got + k > max)
			k = max - got;
		memcpy(out + got, rsp + 1, k);
		got += k;
		if ((size_t)n < ATT_MTU || got >= max)
			return (int)got;
		req[0] = 0x0c;  /* Read Blob */
		put16(req + 1, h);
		put16(req + 3, (uint16_t)got);
		n = att(d, req, 5, rsp, sizeof(rsp));
		if (n == -0x07 || n == -0x0b)   /* Invalid Offset / Attribute Not Long */
			return (int)got;
		if (n < 1)
			return n < 0 ? n : -0x100;
	}
}

static int
att_write(struct ledev *d, uint16_t h, const uint8_t *v, size_t len)
{
	uint8_t req[ATT_MTU], rsp[4];

	if (len > ATT_MTU - 3)
		return -1;
	req[0] = 0x12;
	put16(req + 1, h);
	memcpy(req + 3, v, len);
	return att(d, req, 3 + len, rsp, sizeof(rsp));
}

/* ---------------------------------------------------------------- SMP */

struct smpwant { struct ledev *d; };

static bool
want_smp(const struct item *it, const void *arg)
{
	return it->type == IT_L2CAP && it->d == ((const struct smpwant *)arg)->d
	    && it->cid == CID_SMP && it->len > 0 && it->p[0] != 0x0b;
}

/* next SMP PDU (Pairing Failed included) into out; its length or -1 */
static int
smp_recv(struct ledev *d, uint8_t *out, size_t max)
{
	struct smpwant w = { d };
	struct item it;
	size_t n;

	if (!wait_for(&it, want_smp, &w, d, SMP_TIMEOUT_MS))
		return -1;
	n = it.len < max ? it.len : max;
	memcpy(out, it.p, n);
	return (int)n;
}

static void
smp_fail(struct ledev *d, uint8_t reason)
{
	uint8_t p[2] = { 0x05, reason };
	(void)l2cap_send(d, CID_SMP, p, 2);
}

/* c1 and s1, Vol 3 Part H 2.2.3/2.2.4, as BlueZ bt_crypto_c1/s1 */
static bool
smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
    uint8_t iat, const bdaddr_t *ia, uint8_t rat, const bdaddr_t *ra, uint8_t res[16])
{
	uint8_t p1[16], p2[16];
	int i;

	p1[0] = iat;
	p1[1] = rat;
	memcpy(p1 + 2, preq, 7);
	memcpy(p1 + 9, pres, 7);
	memcpy(p2, ra, 6);
	memcpy(p2 + 6, ia, 6);
	memset(p2 + 12, 0, 4);
	for (i = 0; i < 16; i++)
		res[i] = r[i] ^ p1[i];
	if (!le_encrypt(k, res, res))
		return false;
	for (i = 0; i < 16; i++)
		res[i] ^= p2[i];
	return le_encrypt(k, res, res);
}

static bool
smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t res[16])
{
	memcpy(res, r2, 8);
	memcpy(res + 8, r1, 8);
	return le_encrypt(k, res, res);
}

struct encwant { struct ledev *d; };

static bool
want_enc(const struct item *it, const void *arg)
{
	const struct ledev *d = ((const struct encwant *)arg)->d;

	if (it->type != IT_EVENT)
		return false;
	if (it->code == HCI_EVENT_ENCRYPTION_CHANGE && it->len >= 4 && (le16(it->p + 1) & 0x0fff) == d->handle)
		return true;
	/* LE Enable Encryption refused outright */
	return it->code == HCI_EVENT_COMMAND_STATUS && it->len >= 4 && le16(it->p + 2) == 0x2019 && it->p[0] != 0;
}

/* LE Enable Encryption; 0 when the link is encrypted, else the HCI status */
static int
encrypt_link(struct ledev *d, const uint8_t rnd[8], uint16_t ediv, const uint8_t ltk[16])
{
	struct encwant w = { d };
	struct item it;
	uint8_t cp[28];

	put16(cp, (uint16_t)d->handle);
	memcpy(cp + 2, rnd, 8);
	put16(cp + 10, ediv);
	memcpy(cp + 12, ltk, 16);
	if (hci_cmd_send(0x2019, cp, sizeof(cp)) < 0)
		return -1;
	if (!wait_for(&it, want_enc, &w, d, 15000))
		return -1;
	if (it.code == HCI_EVENT_COMMAND_STATUS)
		return it.p[0];
	if (it.p[0] != 0)
		return it.p[0];
	return it.p[3] ? 0 : -1;
}

/* LE legacy pairing, Just Works, as initiator; the keys go to d */
static bool
smp_pair(struct ledev *d)
{
	uint8_t preq[7] = { 0x01, 0x03 /* NoInputNoOutput */, 0x00 /* no OOB */,
	    0x01 /* bonding */, 16, 0x00 /* we give no keys */, 0x01 /* EncKey */ };
	uint8_t pres[7], buf[32], tk[16], mrand[16], srand[16], mconf[17], sconf[16], chk[16], stk[16];
	uint8_t want;
	int n, st;

	memset(tk, 0, sizeof(tk));      /* Just Works */
	arc4random_buf(mrand, sizeof(mrand));
	if (l2cap_send(d, CID_SMP, preq, sizeof(preq)) < 0)
		return false;
	n = smp_recv(d, buf, sizeof(buf));
	if (n < 7 || buf[0] != 0x02) {
		if (n >= 2 && buf[0] == 0x05)
			logmsg(LOG_ERR, "%s: pairing refused, reason 0x%02x", addr(&d->addr), buf[1]);
		else
			logmsg(LOG_ERR, "%s: no Pairing Response (%d)", addr(&d->addr), n);
		return false;
	}
	memcpy(pres, buf, 7);
	logmsg(LOG_INFO, "%s: pairing response: io 0x%02x auth 0x%02x key size %u keys 0x%02x/0x%02x",
	    addr(&d->addr), pres[1], pres[3], pres[4], pres[5], pres[6]);
	d->keysize = pres[4] < 16 ? pres[4] : 16;
	if (d->keysize < 7) {
		smp_fail(d, 0x06);      /* Encryption Key Size */
		return false;
	}
	if (!smp_c1(tk, mrand, preq, pres, 0 /* public */, &local, d->atype, &d->addr, mconf + 1))
		return false;
	mconf[0] = 0x03;
	if (l2cap_send(d, CID_SMP, mconf, 17) < 0)
		return false;
	n = smp_recv(d, buf, sizeof(buf));
	if (n < 17 || buf[0] != 0x03) {
		logmsg(LOG_ERR, "%s: no Pairing Confirm (pdu 0x%02x)", addr(&d->addr), n > 0 ? buf[0] : 0);
		return false;
	}
	memcpy(sconf, buf + 1, 16);
	buf[0] = 0x04;
	memcpy(buf + 1, mrand, 16);
	if (l2cap_send(d, CID_SMP, buf, 17) < 0)
		return false;
	n = smp_recv(d, buf, sizeof(buf));
	if (n < 17 || buf[0] != 0x04) {
		logmsg(LOG_ERR, "%s: no Pairing Random (pdu 0x%02x%s)", addr(&d->addr),
		    n > 0 ? buf[0] : 0, n > 1 && buf[0] == 0x05 ? ", failed" : "");
		return false;
	}
	memcpy(srand, buf + 1, 16);
	if (!smp_c1(tk, srand, preq, pres, 0, &local, d->atype, &d->addr, chk))
		return false;
	if (memcmp(chk, sconf, 16) != 0) {
		logmsg(LOG_ERR, "%s: the device's confirm value does not check", addr(&d->addr));
		smp_fail(d, 0x04);      /* Confirm Value Failed */
		return false;
	}
	if (!smp_s1(tk, srand, mrand, stk))
		return false;
	memset(stk + d->keysize, 0, 16 - d->keysize);
	{
		uint8_t zero[8] = { 0 };
		st = encrypt_link(d, zero, 0, stk);
	}
	if (st != 0) {
		logmsg(LOG_ERR, "%s: encrypting with the STK failed (0x%02x)", addr(&d->addr), st);
		return false;
	}
	/* key distribution: the responder's LTK, then EDIV and Rand */
	want = pres[6] & 0x01;
	if (!want) {
		logmsg(LOG_ERR, "%s: the device distributes no LTK; cannot bond", addr(&d->addr));
		return false;
	}
	{
		bool got_ltk = false, got_id = false;
		while (!(got_ltk && got_id)) {
			n = smp_recv(d, buf, sizeof(buf));
			if (n < 1)
				return false;
			if (buf[0] == 0x06 && n >= 17) {
				memcpy(d->ltk, buf + 1, 16);
				got_ltk = true;
			} else if (buf[0] == 0x07 && n >= 11) {
				d->ediv = le16(buf + 1);
				memcpy(d->rand, buf + 3, 8);
				got_id = true;
			} else if (buf[0] == 0x05) {
				logmsg(LOG_ERR, "%s: pairing failed during key distribution (0x%02x)",
				    addr(&d->addr), n > 1 ? buf[1] : 0);
				return false;
			}
			/* Identity / Signing information: not asked for, ignored */
		}
	}
	d->bonded = true;
	logmsg(LOG_NOTICE, "%s: paired (key size %u)", addr(&d->addr), d->keysize);
	return true;
}

/* ---------------------------------------------------------------- GATT / HOGP */

struct chr { uint16_t decl, vh, uuid; uint8_t props; };

static bool
gatt_hid(struct ledev *d)
{
	uint8_t req[7], rsp[ATT_MTU], v[8];
	uint16_t s = 1, hs_ = 0, he = 0;
	struct chr ch[48];
	int nch = 0, n, i;

	/* primary services: find HID */
	while (hs_ == 0) {
		req[0] = 0x10;
		put16(req + 1, s);
		put16(req + 3, 0xffff);
		put16(req + 5, 0x2800);
		n = att(d, req, 7, rsp, sizeof(rsp));
		if (n < 2)
			break;
		{
			int el = rsp[1], off;
			uint16_t last = s;
			if (el < 6)
				break;
			for (off = 2; off + el <= n; off += el) {
				uint16_t a = le16(rsp + off), b = le16(rsp + off + 2);
				if (el == 6 && le16(rsp + off + 4) == 0x1812 && hs_ == 0) {
					hs_ = a;
					he = b;
				}
				last = b;
			}
			if (last == 0xffff)
				break;
			s = (uint16_t)(last + 1);
		}
	}
	if (hs_ == 0) {
		logmsg(LOG_ERR, "%s: no HID service", addr(&d->addr));
		return false;
	}

	/* its characteristics */
	s = hs_;
	while (s <= he && nch < (int)(sizeof(ch) / sizeof(ch[0]))) {
		req[0] = 0x08;
		put16(req + 1, s);
		put16(req + 3, he);
		put16(req + 5, 0x2803);
		n = att(d, req, 7, rsp, sizeof(rsp));
		if (n < 2)
			break;
		{
			int el = rsp[1], off;
			uint16_t last = s;
			if (el < 7)
				break;
			for (off = 2; off + el <= n && nch < (int)(sizeof(ch) / sizeof(ch[0])); off += el) {
				ch[nch].decl = le16(rsp + off);
				ch[nch].props = rsp[off + 2];
				ch[nch].vh = le16(rsp + off + 3);
				ch[nch].uuid = el == 7 ? le16(rsp + off + 5) : 0;
				last = ch[nch].vh;
				nch++;
			}
			if (last >= he)
				break;
			s = (uint16_t)(last + 1);
		}
	}

	/* Report Map */
	for (i = 0; i < nch; i++) {
		if (ch[i].uuid != 0x2a4b)
			continue;
		d->map = malloc(4096);
		if (d->map == NULL)
			return false;
		n = att_read(d, ch[i].vh, d->map, 4096);
		if (n <= 0) {
			logmsg(LOG_ERR, "%s: reading the Report Map failed (%d)", addr(&d->addr), n);
			return false;
		}
		d->maplen = (size_t)n;
		break;
	}
	if (d->maplen == 0) {
		logmsg(LOG_ERR, "%s: no Report Map", addr(&d->addr));
		return false;
	}

	/* input Reports: Report Reference, then notifications on */
	d->nin = 0;
	d->ids = false;
	for (i = 0; i < nch && d->nin < MAXIN; i++) {
		uint16_t ds, de, rr = 0, cccd = 0;
		if (ch[i].uuid != 0x2a4d || !(ch[i].props & 0x10))     /* Notify */
			continue;
		ds = (uint16_t)(ch[i].vh + 1);
		de = i + 1 < nch ? (uint16_t)(ch[i + 1].decl - 1) : he;
		while (ds <= de) {
			int off;
			req[0] = 0x04;
			put16(req + 1, ds);
			put16(req + 3, de);
			n = att(d, req, 5, rsp, sizeof(rsp));
			if (n < 2 || rsp[1] != 0x01)
				break;
			for (off = 2; off + 4 <= n; off += 4) {
				uint16_t h = le16(rsp + off), u = le16(rsp + off + 2);
				if (u == 0x2908) rr = h;
				if (u == 0x2902) cccd = h;
				ds = (uint16_t)(h + 1);
			}
			if (ds == 0)
				break;
		}
		if (rr == 0 || cccd == 0)
			continue;
		n = att_read(d, rr, v, sizeof(v));
		if (n < 2 || v[1] != 0x01)              /* Input */
			continue;
		{
			uint8_t id = v[0], on[2] = { 0x01, 0x00 };      /* notifications */
			if (att_write(d, cccd, on, 2) < 0) {
				logmsg(LOG_ERR, "%s: enabling report %u failed", addr(&d->addr), id);
				continue;
			}
			d->in[d->nin].id = id;
		}
		d->in[d->nin].vh = ch[i].vh;
		if (d->in[d->nin].id != 0)
			d->ids = true;
		d->nin++;
	}
	if (d->nin == 0) {
		logmsg(LOG_ERR, "%s: no input reports", addr(&d->addr));
		return false;
	}

	/* PnP ID, wherever it is (Device Information service) */
	req[0] = 0x08;
	put16(req + 1, 0x0001);
	put16(req + 3, 0xffff);
	put16(req + 5, 0x2a50);
	n = att(d, req, 7, rsp, sizeof(rsp));
	if (n >= 2 + 2 + 7 && rsp[1] >= 9) {
		d->vendor = le16(rsp + 5);
		d->product = le16(rsp + 7);
	}
	return true;
}

/* Everything after LE Connection Complete. */
static void
dev_setup(struct ledev *d)
{
	int st;

	d->setup = false;
	if (d->pairing) {
		if (!smp_pair(d)) {
			d->pairing = false;
			d->failed = true;
			d->bonded = false;
			dev_disconnect(d, "pairing failed");
			return;
		}
		d->pairing = false;
		save_db();
	} else if (d->bonded) {
		st = encrypt_link(d, d->rand, d->ediv, d->ltk);
		if (st != 0) {
			logmsg(LOG_ERR, "%s: encryption failed (0x%02x)%s", addr(&d->addr), st,
			    st == 0x06 ? ": the device no longer has our key; pair it again" : "");
			if (st == 0x06) {
				d->bonded = false;
				d->failed = true;
				save_db();
			}
			dev_disconnect(d, "not encrypted");
			return;
		}
		logmsg(LOG_INFO, "%s: encrypted", addr(&d->addr));
	} else {
		dev_disconnect(d, "not bonded");
		return;
	}
	if (d->handle < 0)
		return;
	if (!gatt_hid(d)) {
		dev_disconnect(d, "HID service unusable");
		return;
	}
	d->hid = hid_create(d);
	if (d->hid == MACH_PORT_NULL) {
		dev_disconnect(d, "no HID device");
		return;
	}
	d->ready = true;
}

/* ---------------------------------------------------------------- connecting */

static void
connect_cancel(void)
{
	if (!connecting)
		return;
	(void)hci_cmd(0x200e, NULL, 0, NULL, 0);        /* LE Create Connection Cancel */
	/* LE Connection Complete (status 0x02) clears `connecting` */
	connecting = false;
	connecting_for[0] = 0;
}

/*
 * Keep one LE Create Connection pending for what should be connected:
 * the device being paired alone, else every bonded device that is not
 * connected, through the White List.
 */
static void
connect_policy(void)
{
	char want[sizeof(connecting_for)];
	struct ledev *pair = NULL;
	uint8_t cp[25];
	size_t len = 0;
	int i, st, n = 0;

	want[0] = 0;
	for (i = 0; i < MAXDEV; i++) {
		struct ledev *d = &dev[i];
		if (d->used && d->pairing && !d->failed && d->handle < 0) {
			pair = d;
			if (time(NULL) - d->pair_since > PAIR_GIVEUP) {
				logmsg(LOG_ERR, "%s: not found for pairing", addr(&d->addr));
				d->pairing = false;
				d->failed = true;
				pair = NULL;
			}
			break;
		}
	}
	if (pair != NULL)
		snprintf(want, sizeof(want), "P%s", addr(&pair->addr));
	else {
		for (i = 0; i < MAXDEV; i++) {
			struct ledev *d = &dev[i];
			if (d->used && d->bonded && !d->pairing && d->handle < 0) {
				len += (size_t)snprintf(want + len, sizeof(want) - len, "%s ", addr(&d->addr));
				n++;
			}
		}
	}
	if (connecting && strcmp(want, connecting_for) == 0)
		return;
	connect_cancel();
	if (want[0] == 0)
		return;

	memset(cp, 0, sizeof(cp));
	put16(cp, 0x0060);              /* scan interval 60 ms */
	put16(cp + 2, 0x0030);          /* scan window 30 ms */
	if (pair != NULL) {
		cp[4] = 0x00;           /* the peer address below */
		cp[5] = pair->atype;
		memcpy(cp + 6, &pair->addr, 6);
	} else {
		(void)hci_cmd(0x2010, NULL, 0, NULL, 0);        /* LE Clear White List */
		for (i = 0; i < MAXDEV; i++) {
			struct ledev *d = &dev[i];
			uint8_t wl[7];
			if (!(d->used && d->bonded && !d->pairing && d->handle < 0))
				continue;
			wl[0] = d->atype;
			memcpy(wl + 1, &d->addr, 6);
			(void)hci_cmd(0x2011, wl, sizeof(wl), NULL, 0);
		}
		cp[4] = 0x01;           /* the White List */
	}
	cp[12] = 0x00;                  /* own address: public */
	put16(cp + 13, 0x0006);         /* interval 7.5 ms .. */
	put16(cp + 15, 0x000c);         /* .. 15 ms */
	put16(cp + 17, 0);              /* latency */
	put16(cp + 19, 0x01f4);         /* supervision timeout 5 s */
	put16(cp + 21, 0);
	put16(cp + 23, 0);
	st = hci_cmd(0x200d, cp, sizeof(cp), NULL, 0);
	if (st != 0) {
		logmsg(LOG_ERR, "LE Create Connection: status 0x%02x", st);
		return;
	}
	connecting = true;
	strlcpy(connecting_for, want, sizeof(connecting_for));
	if (pair != NULL)
		logmsg(LOG_INFO, "%s: waiting for it to advertise (pairing)", addr(&pair->addr));
	else
		logmsg(LOG_INFO, "waiting for %d bonded device%s to advertise", n, n == 1 ? "" : "s");
}

/* ---------------------------------------------------------------- main */

static void
on_signal(int sig)
{
	char c = (char)sig;
	(void)write(sigpipe[1], &c, 1);
}

static bool
open_unit(void)
{
	struct bt_devfilter f;
	struct bt_devinfo info;

	if (hs >= 0)
		return true;
	if (bt_devinfo(UNIT, &info) < 0 || !(info.enabled))
		return false;
	hs = bt_devopen(UNIT, BTOPT_DIRECTION);
	if (hs < 0)
		return false;
	memset(&f, 0, sizeof(f));
	bt_devfilter_pkt_set(&f, HCI_EVENT_PKT);
	bt_devfilter_pkt_set(&f, HCI_ACL_DATA_PKT);
	bt_devfilter_evt_set(&f, HCI_EVENT_COMMAND_COMPL);
	bt_devfilter_evt_set(&f, HCI_EVENT_COMMAND_STATUS);
	bt_devfilter_evt_set(&f, HCI_EVENT_DISCON_COMPL);
	bt_devfilter_evt_set(&f, HCI_EVENT_ENCRYPTION_CHANGE);
	bt_devfilter_evt_set(&f, HCI_LE_META_EVENT);
	if (bt_devfilter(hs, &f, NULL) < 0 || bt_devaddr(UNIT, &local) < 0) {
		close(hs);
		hs = -1;
		return false;
	}
	{
		uint8_t m[8] = { 0x1f, 0, 0, 0, 0, 0, 0, 0 };   /* LE events: the default set */
		(void)hci_cmd(HCI_CMD_LE_SET_EVENT_MASK, m, sizeof(m), NULL, 0);
	}
	logmsg(LOG_NOTICE, "%s up, local address %s", UNIT, addr(&local));
	return true;
}

/* The unit went down (btsetup power off): every LE link went with it. */
static void
close_unit(void)
{
	int i;

	if (hs < 0)
		return;
	for (i = 0; i < MAXDEV; i++)
		if (dev[i].used)
			dev_lost(&dev[i]);
	close(hs);
	hs = -1;
	connecting = false;
	connecting_for[0] = 0;
	logmsg(LOG_NOTICE, "%s down", UNIT);
}

int
main(int argc, char **argv)
{
	FILE *f;
	int ch, i;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		if (ch == 'd')
			debug = true;
		else {
			fprintf(stderr, "usage: blehidd [-d]\n");
			return 1;
		}
	}
	openlog("blehidd", LOG_PID, LOG_DAEMON);
	if (pipe(sigpipe) < 0)
		return 1;
	(void)fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
	signal(SIGHUP, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);
	if ((f = fopen(PIDFILE, "w")) != NULL) {
		fprintf(f, "%d\n", (int)getpid());
		fclose(f);
	}
	load_config();
	logmsg(LOG_NOTICE, "started");

	for (;;) {
		struct pollfd pfd[2];
		struct bt_devinfo info;
		int n = 0;

		unit_up = bt_devinfo(UNIT, &info) == 0 && info.enabled;
		if (!unit_up)
			close_unit();
		else if (hs < 0)
			(void)open_unit();
		if (hs >= 0) {
			for (i = 0; i < MAXDEV; i++)
				if (dev[i].used && dev[i].setup && dev[i].handle >= 0)
					dev_setup(&dev[i]);
			connect_policy();
		}
		write_status();

		pfd[n].fd = sigpipe[0];
		pfd[n++].events = POLLIN;
		if (hs >= 0) {
			pfd[n].fd = hs;
			pfd[n++].events = POLLIN;
		}
		if (poll(pfd, (nfds_t)n, 1000) < 0 && errno != EINTR) {
			sleep(1);
			continue;
		}
		if (pfd[0].revents) {
			char c;
			while (read(sigpipe[0], &c, 1) == 1) {
				if (c == SIGHUP) {
					logmsg(LOG_NOTICE, "SIGHUP: rereading %s", DBFILE);
					load_config();
				} else {
					for (i = 0; i < MAXDEV; i++)
						if (dev[i].used)
							dev_disconnect(&dev[i], "exiting");
					connect_cancel();
					unlink(PIDFILE);
					unlink(STATUSFILE);
					return 0;
				}
			}
		}
		if (hs >= 0 && n > 1 && pfd[1].revents) {
			struct item it;
			/* drain what is there, without waiting */
			do {
				next_item(&it, 0);
				handle_item(&it);
			} while (it.type != IT_NONE);
		}
	}
}
