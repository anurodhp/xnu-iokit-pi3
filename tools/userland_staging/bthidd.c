/*
 * bthidd -- Bluetooth HID host for this port (docs/bluetooth-stack-plan.md
 * S5). Serves Bluetooth keyboards, mice and other HID devices over the
 * kernel's L2CAP sockets (NetBSD netbt) and presents each one to IOKit as an
 * IOHIDUserDevice, the same path a USB HID device takes to X
 * (IOHIDEventDriver -> IOHIDEventService -> xf86-input-iohid).
 *
 * NetBSD does this in the kernel (sys/dev/bluetooth/bthidev.c, feeding
 * wscons through btkbd/btms); there is no wscons here and no BSD has a
 * user-space HID host over netbt's socket API, so this daemon follows
 * bthidev.c's protocol handling (citations below) in user space:
 *
 *   devices    every entry of btdevctl(8)'s cache (/var/db/btdevctl.plist,
 *              usr.sbin/btdevctl/db.c) with device-type "bthidev" that
 *              `btdevctl -A` marked "attached" (our fork's Darwin path in
 *              btdevctl.c). SIGHUP rereads it. The cache holds what
 *              btdevctl learnt over SDP (sdp.c): the HID descriptor, the
 *              control and interrupt PSMs, vendor/product ids, the link mode,
 *              and "reconnect" -- set when the device does NOT initiate
 *              reconnection (HIDReconnectInitiate false), so the host must.
 *   channels   bthidev_listen / bthidev_connect: listen on the control and
 *              interrupt PSMs for devices that reconnect themselves; connect
 *              control first, then interrupt, for the others (and once at
 *              attach for all, as bthidev's BTHID_CONNECTING does). The link
 *              mode (auth/encrypt/secure) goes on every socket as
 *              SO_L2CAP_LM, as bthidev's l2cap_setopt(sc_mode).
 *   reports    bthidev_process_one: a DATA|INPUT packet on either channel
 *              carries an input report after its header byte; it goes to
 *              the IOHIDUserDevice unchanged (report id included, as the
 *              descriptor defines it). CONTROL|UNPLUG closes both channels.
 *              Output reports (keyboard LEDs) are not forwarded yet.
 *   reconnect  bthidev_timeout: a host-reconnect device that dropped is
 *              connected again after a delay.
 *
 * The IOHIDUserDevice user client needs the
 * com.apple.hid.manager.user-access-device entitlement
 * (xnu IOHIDResourceUserClient.cpp:101-131); build_bthidd.sh signs it in.
 * The device creation sequence is hid_userdevice_test.c's (step H0).
 *
 * Usage: bthidd [-d]     (-d: log to stderr too; runs in the foreground)
 */
#include <sys/types.h>
#include <sys/socket.h>
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
#include <netbt/l2cap.h>
#include <dev/bluetooth/btdev.h>
#include <dev/bluetooth/bthidev.h>
#include <dev/bluetooth/bthid.h>
#include <prop/proplib.h>

#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFSerialize.h>
#include <CoreFoundation/CoreFoundation.h>

#define BTDEVCTL_PLIST  "/var/db/btdevctl.plist"        /* btdevctl db.c */
#define PIDFILE         "/var/run/bthidd.pid"           /* btdevctl.c, Darwin path */
#define RECONNECT_SECS  5
#define MAXDEV          16

enum { kResourceTypeDevice = 0 };                       /* IOHIDResourceUserClient.h */
enum { kMethodCreate = 0, kMethodTerminate = 1, kMethodHandleReport = 2 };

struct hiddev {
	bool            used, seen;
	bdaddr_t        laddr, raddr;
	int             ctlpsm, intpsm;
	int             lm;             /* SO_L2CAP_LM */
	bool            host_reconnect; /* BTHIDEVreconnect */
	uint16_t        vendor, product;
	uint8_t        *desc;
	size_t          desclen;
	int             ctl, intr;      /* L2CAP sockets, -1 if closed */
	bool            ctl_connecting, int_connecting;
	io_connect_t    hid;            /* IOHIDUserDevice, MACH_PORT_NULL if none */
	time_t          retry_at;       /* host reconnect: next attempt */
};

static struct hiddev dev[MAXDEV];
static int lctl = -1, lint = -1;        /* listening sockets */
static int sigpipe[2];
static bool debug;

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

/* ---------------------------------------------------------------- IOKit */

static void
cfset(CFMutableDictionaryRef d, const char *key, CFTypeRef v)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	CFDictionarySetValue(d, k, v);
	CFRelease(k);
	CFRelease(v);
}

static CFNumberRef
cfnum(int v)
{
	return CFNumberCreate(NULL, kCFNumberIntType, &v);
}

static CFStringRef
cfstr(const char *s)
{
	return CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
}

static io_connect_t
hid_create(struct hiddev *d)
{
	io_service_t res;
	io_connect_t conn = MACH_PORT_NULL;
	CFMutableDictionaryRef p;
	CFDataRef ser;
	char name[64];
	uint64_t options = 0;
	kern_return_t kr;

	res = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOHIDResource"));
	if (res == MACH_PORT_NULL) {
		logmsg(LOG_ERR, "no IOHIDResource service");
		return MACH_PORT_NULL;
	}
	p = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	cfset(p, "ReportDescriptor", CFDataCreate(NULL, d->desc, (CFIndex)d->desclen));
	cfset(p, "VendorID", cfnum(d->vendor));
	cfset(p, "ProductID", cfnum(d->product));
	snprintf(name, sizeof(name), "Bluetooth HID %s", addr(&d->raddr));
	cfset(p, "Product", cfstr(name));
	cfset(p, "SerialNumber", cfstr(addr(&d->raddr)));
	cfset(p, "Transport", cfstr("Bluetooth"));
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
		logmsg(LOG_ERR, "%s: IOHIDUserDevice create failed (0x%x)", addr(&d->raddr), kr);
	else
		logmsg(LOG_NOTICE, "%s: HID device up (vendor 0x%04x product 0x%04x, %zu-byte descriptor)",
		    addr(&d->raddr), d->vendor, d->product, d->desclen);
	return conn;
}

static void
hid_destroy(struct hiddev *d)
{
	if (d->hid == MACH_PORT_NULL)
		return;
	(void)IOConnectCallMethod(d->hid, kMethodTerminate, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
	IOServiceClose(d->hid);
	d->hid = MACH_PORT_NULL;
	logmsg(LOG_NOTICE, "%s: HID device down", addr(&d->raddr));
}

static void
hid_report(struct hiddev *d, const uint8_t *r, size_t len)
{
	uint64_t ts = 0;        /* 0: the kernel timestamps it */

	if (d->hid == MACH_PORT_NULL || len == 0)
		return;
	(void)IOConnectCallMethod(d->hid, kMethodHandleReport, &ts, 1, r, len, NULL, NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- L2CAP */

static int
l2cap_socket(int lm)
{
	int s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

	if (s < 0)
		return -1;
	if (lm != 0 && setsockopt(s, BTPROTO_L2CAP, SO_L2CAP_LM, &lm, sizeof(lm)) < 0)
		logmsg(LOG_WARNING, "SO_L2CAP_LM %d: %s", lm, strerror(errno));
	return s;
}

static int
l2cap_listen(int psm)
{
	struct sockaddr_bt sa;
	int s = l2cap_socket(0);

	if (s < 0)
		return -1;
	memset(&sa, 0, sizeof(sa));
	sa.bt_len = sizeof(sa);
	sa.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa.bt_bdaddr, BDADDR_ANY);
	sa.bt_psm = (uint16_t)psm;
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(s, 4) < 0) {
		logmsg(LOG_ERR, "listen on PSM 0x%x: %s", psm, strerror(errno));
		close(s);
		return -1;
	}
	return s;
}

/* Non-blocking connect; completion shows up as POLLOUT. */
static int
l2cap_connect(struct hiddev *d, int psm)
{
	struct sockaddr_bt sa;
	int s = l2cap_socket(d->lm);

	if (s < 0)
		return -1;
	memset(&sa, 0, sizeof(sa));
	sa.bt_len = sizeof(sa);
	sa.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa.bt_bdaddr, &d->laddr);
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(s);
		return -1;
	}
	(void)fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
	sa.bt_psm = (uint16_t)psm;
	bdaddr_copy(&sa.bt_bdaddr, &d->raddr);
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 && errno != EINPROGRESS) {
		logmsg(LOG_INFO, "%s: connect PSM 0x%x: %s", addr(&d->raddr), psm, strerror(errno));
		close(s);
		return -1;
	}
	return s;
}

static void
dev_close(struct hiddev *d, const char *why)
{
	if (d->ctl >= 0 || d->intr >= 0)
		logmsg(LOG_INFO, "%s: closed (%s)", addr(&d->raddr), why);
	hid_destroy(d);
	if (d->intr >= 0)
		close(d->intr);
	if (d->ctl >= 0)
		close(d->ctl);
	d->intr = d->ctl = -1;
	d->ctl_connecting = d->int_connecting = false;
	d->retry_at = d->host_reconnect ? time(NULL) + RECONNECT_SECS : 0;
}

/* Both channels open: the device is up (bthidev_int_connected). */
static void
dev_check_open(struct hiddev *d)
{
	if (d->ctl >= 0 && d->intr >= 0 && !d->ctl_connecting && !d->int_connecting
	    && d->hid == MACH_PORT_NULL)
		d->hid = hid_create(d);
}

static void
dev_connect(struct hiddev *d)
{
	if (d->ctl >= 0 || d->intr >= 0)
		return;
	d->ctl = l2cap_connect(d, d->ctlpsm);
	if (d->ctl < 0) {
		d->retry_at = d->host_reconnect ? time(NULL) + RECONNECT_SECS : 0;
		return;
	}
	d->ctl_connecting = true;
	logmsg(LOG_INFO, "%s: connecting", addr(&d->raddr));
}

/* One packet from the device (bthidev_process_one). */
static void
dev_input(struct hiddev *d, int s)
{
	uint8_t buf[1024];
	ssize_t n = recv(s, buf, sizeof(buf), 0);

	if (n <= 0) {
		dev_close(d, n == 0 ? "disconnected" : strerror(errno));
		return;
	}
	switch (BTHID_TYPE(buf[0])) {
	case BTHID_DATA:
		if (BTHID_DATA_PARAM(buf[0]) == BTHID_DATA_INPUT)
			hid_report(d, buf + 1, (size_t)n - 1);
		break;
	case BTHID_CONTROL:
		if (BTHID_CONTROL_PARAM(buf[0]) == BTHID_CONTROL_UNPLUG)
			dev_close(d, "virtual cable unplug");
		break;
	default:
		break;          /* HANDSHAKE and the rest: nothing to do */
	}
}

/* ---------------------------------------------------------------- config */

static int
link_mode(prop_dictionary_t p)
{
	const char *mode;

	if (!prop_dictionary_get_string(p, BTDEVmode, &mode))
		return 0;
	if (strcmp(mode, BTDEVauth) == 0)
		return L2CAP_LM_AUTH;
	if (strcmp(mode, BTDEVencrypt) == 0)
		return L2CAP_LM_ENCRYPT;
	if (strcmp(mode, BTDEVsecure) == 0)
		return L2CAP_LM_SECURE;
	return 0;
}

static struct hiddev *
find_dev(const bdaddr_t *raddr)
{
	for (int i = 0; i < MAXDEV; i++)
		if (dev[i].used && bdaddr_same(&dev[i].raddr, raddr))
			return &dev[i];
	return NULL;
}

/*
 * Reread the btdevctl cache: { local bdaddr string: { remote bdaddr string:
 * { service: device dictionary } } } (usr.sbin/btdevctl/db.c db_get).
 */
static void
load_config(void)
{
	prop_dictionary_t db, ldict, rdict, p;
	prop_object_iterator_t li, ri, si;
	prop_dictionary_keysym_t lk, rk, sk;
	int i;

	for (i = 0; i < MAXDEV; i++)
		dev[i].seen = false;
	db = prop_dictionary_internalize_from_file(BTDEVCTL_PLIST);
	if (db != NULL && (li = prop_dictionary_iterator(db)) != NULL) {
		while ((lk = prop_object_iterator_next(li)) != NULL) {
			ldict = prop_dictionary_get_keysym(db, lk);
			if (prop_object_type(ldict) != PROP_TYPE_DICTIONARY)
				continue;
			ri = prop_dictionary_iterator(ldict);
			while (ri != NULL && (rk = prop_object_iterator_next(ri)) != NULL) {
				rdict = prop_dictionary_get_keysym(ldict, rk);
				if (prop_object_type(rdict) != PROP_TYPE_DICTIONARY)
					continue;
				si = prop_dictionary_iterator(rdict);
				while (si != NULL && (sk = prop_object_iterator_next(si)) != NULL) {
					const char *type;
					bool attached = false, reconnect = false;
					bdaddr_t laddr, raddr;
					prop_data_t desc;
					int32_t cpsm, ipsm;
					uint16_t v = 0, pr = 0;
					struct hiddev *d;

					p = prop_dictionary_get_keysym(rdict, sk);
					if (prop_object_type(p) != PROP_TYPE_DICTIONARY
					    || !prop_dictionary_get_string(p, BTDEVtype, &type)
					    || strcmp(type, "bthidev") != 0
					    || !prop_dictionary_get_bool(p, "attached", &attached)
					    || !attached
					    || !bt_aton(prop_dictionary_keysym_value(lk), &laddr)
					    || !bt_aton(prop_dictionary_keysym_value(rk), &raddr)
					    || !prop_dictionary_get_int32(p, BTHIDEVcontrolpsm, &cpsm)
					    || !prop_dictionary_get_int32(p, BTHIDEVinterruptpsm, &ipsm)
					    || (desc = prop_dictionary_get(p, BTHIDEVdescriptor)) == NULL
					    || prop_data_size(desc) == 0)
						continue;
					(void)prop_dictionary_get_bool(p, BTHIDEVreconnect, &reconnect);
					(void)prop_dictionary_get_uint16(p, BTDEVvendor, &v);
					(void)prop_dictionary_get_uint16(p, BTDEVproduct, &pr);

					d = find_dev(&raddr);
					if (d == NULL) {
						for (i = 0; i < MAXDEV && dev[i].used; i++)
							;
						if (i == MAXDEV) {
							logmsg(LOG_WARNING, "more than %d HID devices", MAXDEV);
							continue;
						}
						d = &dev[i];
						memset(d, 0, sizeof(*d));
						d->used = true;
						d->ctl = d->intr = -1;
						d->hid = MACH_PORT_NULL;
						bdaddr_copy(&d->raddr, &raddr);
						d->retry_at = time(NULL);       /* connect once at attach */
						logmsg(LOG_NOTICE, "%s: attached (%s reconnects)", addr(&raddr),
						    reconnect ? "host" : "device");
					}
					d->seen = true;
					bdaddr_copy(&d->laddr, &laddr);
					d->ctlpsm = cpsm;
					d->intpsm = ipsm;
					d->lm = link_mode(p);
					d->host_reconnect = reconnect;
					d->vendor = v;
					d->product = pr;
					free(d->desc);
					d->desclen = prop_data_size(desc);
					d->desc = malloc(d->desclen);
					if (d->desc != NULL)
						memcpy(d->desc, prop_data_value(desc), d->desclen);
					else
						d->desclen = 0;
				}
				if (si != NULL)
					prop_object_iterator_release(si);
			}
			if (ri != NULL)
				prop_object_iterator_release(ri);
		}
		prop_object_iterator_release(li);
	}
	if (db != NULL)
		prop_object_release(db);

	for (i = 0; i < MAXDEV; i++) {
		if (dev[i].used && !dev[i].seen) {
			logmsg(LOG_NOTICE, "%s: detached", addr(&dev[i].raddr));
			dev[i].host_reconnect = false;
			dev_close(&dev[i], "detached");
			free(dev[i].desc);
			dev[i].used = false;
		}
	}
}

/* ---------------------------------------------------------------- main */

static void
on_signal(int sig)
{
	char c = (char)sig;
	(void)write(sigpipe[1], &c, 1);
}

static void
accept_channel(int l, bool control)
{
	struct sockaddr_bt sa;
	socklen_t len = sizeof(sa);
	struct hiddev *d;
	int s = accept(l, (struct sockaddr *)&sa, &len);

	if (s < 0)
		return;
	d = find_dev(&sa.bt_bdaddr);
	if (d == NULL) {
		logmsg(LOG_INFO, "%s: refused %s channel (not an attached HID device)",
		    addr(&sa.bt_bdaddr), control ? "control" : "interrupt");
		close(s);
		return;
	}
	if (d->lm != 0)
		(void)setsockopt(s, BTPROTO_L2CAP, SO_L2CAP_LM, &d->lm, sizeof(d->lm));
	if (control) {
		if (d->ctl >= 0)
			dev_close(d, "new control channel");
		d->ctl = s;
		d->ctl_connecting = false;
	} else {
		if (d->ctl < 0) {       /* the interrupt channel follows the control one */
			close(s);
			return;
		}
		if (d->intr >= 0)
			close(d->intr);
		d->intr = s;
		d->int_connecting = false;
	}
	logmsg(LOG_INFO, "%s: device opened its %s channel", addr(&d->raddr),
	    control ? "control" : "interrupt");
	dev_check_open(d);
}

int
main(int argc, char **argv)
{
	FILE *f;
	int ch;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		if (ch == 'd')
			debug = true;
		else {
			fprintf(stderr, "usage: bthidd [-d]\n");
			return 1;
		}
	}
	openlog("bthidd", LOG_PID, LOG_DAEMON);
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

	lctl = l2cap_listen(L2CAP_PSM_HID_CNTL);
	lint = l2cap_listen(L2CAP_PSM_HID_INTR);
	load_config();
	logmsg(LOG_NOTICE, "started (listening on PSMs 0x11/0x13: %s)",
	    lctl >= 0 && lint >= 0 ? "yes" : "no");

	for (;;) {
		struct pollfd pfd[2 + 2 + 2 * MAXDEV];
		struct hiddev *owner[2 + 2 + 2 * MAXDEV];
		int n = 0, i, timeout = 1000;
		time_t now = time(NULL);

		for (i = 0; i < MAXDEV; i++) {
			struct hiddev *d = &dev[i];
			if (d->used && d->ctl < 0 && d->retry_at != 0 && now >= d->retry_at) {
				d->retry_at = 0;
				dev_connect(d);
			}
		}
		pfd[n].fd = sigpipe[0]; pfd[n].events = POLLIN; owner[n++] = NULL;
		if (lctl >= 0) { pfd[n].fd = lctl; pfd[n].events = POLLIN; owner[n++] = NULL; }
		if (lint >= 0) { pfd[n].fd = lint; pfd[n].events = POLLIN; owner[n++] = NULL; }
		for (i = 0; i < MAXDEV; i++) {
			struct hiddev *d = &dev[i];
			if (!d->used)
				continue;
			if (d->ctl >= 0) {
				pfd[n].fd = d->ctl;
				pfd[n].events = d->ctl_connecting ? POLLOUT : POLLIN;
				owner[n++] = d;
			}
			if (d->intr >= 0) {
				pfd[n].fd = d->intr;
				pfd[n].events = d->int_connecting ? POLLOUT : POLLIN;
				owner[n++] = d;
			}
		}
		if (poll(pfd, (nfds_t)n, timeout) < 0) {
			if (errno == EINTR)
				continue;
			logmsg(LOG_ERR, "poll: %s", strerror(errno));
			sleep(1);
			continue;
		}
		for (i = 0; i < n; i++) {
			struct hiddev *d = owner[i];
			if (pfd[i].revents == 0)
				continue;
			if (pfd[i].fd == sigpipe[0]) {
				char c;
				while (read(sigpipe[0], &c, 1) == 1) {
					if (c == SIGHUP) {
						logmsg(LOG_NOTICE, "SIGHUP: rereading %s", BTDEVCTL_PLIST);
						load_config();
					} else {
						for (int j = 0; j < MAXDEV; j++)
							if (dev[j].used)
								dev_close(&dev[j], "exiting");
						unlink(PIDFILE);
						return 0;
					}
				}
				break;  /* the device table may have changed */
			}
			if (d == NULL) {
				accept_channel(pfd[i].fd, pfd[i].fd == lctl);
				break;
			}
			if (pfd[i].fd == d->ctl && d->ctl_connecting) {
				int e = 0;
				socklen_t el = sizeof(e);
				(void)getsockopt(d->ctl, SOL_SOCKET, SO_ERROR, &e, &el);
				if (e != 0 || (pfd[i].revents & (POLLERR | POLLHUP))) {
					dev_close(d, e ? strerror(e) : "control connect failed");
					break;
				}
				d->ctl_connecting = false;
				d->intr = l2cap_connect(d, d->intpsm);  /* then interrupt (bthidev_ctl_connected) */
				if (d->intr < 0) {
					dev_close(d, "interrupt connect failed");
					break;
				}
				d->int_connecting = true;
				break;
			}
			if (pfd[i].fd == d->intr && d->int_connecting) {
				int e = 0;
				socklen_t el = sizeof(e);
				(void)getsockopt(d->intr, SOL_SOCKET, SO_ERROR, &e, &el);
				if (e != 0 || (pfd[i].revents & (POLLERR | POLLHUP))) {
					dev_close(d, e ? strerror(e) : "interrupt connect failed");
					break;
				}
				d->int_connecting = false;
				logmsg(LOG_INFO, "%s: connected", addr(&d->raddr));
				dev_check_open(d);
				break;
			}
			dev_input(d, pfd[i].fd);
			break;  /* one event per pass: dev_input may close sockets */
		}
	}
}
