/*
 * wifi - control the Pi 3's on-board WiFi through BCM43430WifiUserClient
 * (docs/wifi-plan.md; ABI in iokit/BCM43430Wifi/BCM43430WifiShared.h).
 *
 *   wifi up [firmware-dir]         load firmware (default /usr/share/firmware/wifi)
 *   wifi scan
 *   wifi join <ssid> [passphrase]  open network without a passphrase, else WPA2-PSK
 *   wifi join -f <file>            file with lines ssid=... and psk=... (e.g. /etc/wifi.conf)
 *   wifi leave
 *   wifi status
 *
 * The kernel needs boot-arg wifi=1 (SD card on sdhost, Arasan controller in
 * SDIO mode). Address configuration is IPConfiguration's job afterwards
 * (ipconfig set en1 DHCP).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "BCM43430WifiShared.h"

#define FW_DIR  "/usr/share/firmware/wifi"
#define FW_BIN  "brcmfmac43430-sdio.bin"
#define FW_TXT  "brcmfmac43430-sdio.txt"
#define FW_CLM  "brcmfmac43430-sdio.clm_blob"

static io_connect_t conn;

static const char *
errstr(kern_return_t kr)
{
	static char b[32];
	switch (kr) {
	case kIOReturnNotReady:      return "not ready";
	case kIOReturnNotPrivileged: return "must be root";
	case kIOReturnTimeout:       return "timed out";
	case kIOReturnBusy:          return "busy";
	case kIOReturnBadArgument:   return "bad argument";
	case kIOReturnIOError:       return "I/O error";
	}
	snprintf(b, sizeof(b), "0x%x", kr);
	return b;
}

static void
lasterror(void)
{
	wifi_status st;
	size_t sz = sizeof(st);
	if (IOConnectCallStructMethod(conn, kWifiMethodStatus, NULL, 0, &st, &sz) == KERN_SUCCESS &&
	    st.lasterror[0]) {
		fprintf(stderr, "wifi: driver says: %s\n", st.lasterror);
	}
}

static int
openDriver(void)
{
	io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching(kBCM43430WifiClass));
	if (svc == IO_OBJECT_NULL) {
		fprintf(stderr, "wifi: no %s service (boot with wifi=1?)\n", kBCM43430WifiClass);
		return -1;
	}
	kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
	IOObjectRelease(svc);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "wifi: IOServiceOpen: %s\n", errstr(kr));
		return -1;
	}
	return 0;
}

static uint8_t *
readFile(const char *dir, const char *name, size_t *len)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "wifi: cannot open %s\n", path);
		return NULL;
	}
	struct stat sb;
	if (fstat(fd, &sb) != 0 || sb.st_size <= 0) {
		close(fd);
		return NULL;
	}
	uint8_t *buf = malloc((size_t)sb.st_size);
	size_t got = 0;
	while (buf && got < (size_t)sb.st_size) {
		ssize_t n = read(fd, buf + got, (size_t)sb.st_size - got);
		if (n <= 0) {
			free(buf);
			buf = NULL;
			break;
		}
		got += (size_t)n;
	}
	close(fd);
	*len = got;
	return buf;
}

static int
upload(uint32_t type, const uint8_t *buf, size_t len)
{
	for (size_t off = 0; off < len; off += kWifiChunkMax) {
		size_t n = len - off > kWifiChunkMax ? kWifiChunkMax : len - off;
		uint64_t in[2] = { type, off };
		kern_return_t kr = IOConnectCallMethod(conn, kWifiMethodUploadChunk, in, 2,
		    buf + off, n, NULL, NULL, NULL, NULL);
		if (kr != KERN_SUCCESS) {
			fprintf(stderr, "wifi: upload type %u at %zu: %s\n", type, off, errstr(kr));
			lasterror();
			return -1;
		}
	}
	return 0;
}

static int
cmdUp(const char *dir)
{
	size_t blen = 0, tlen = 0, clen = 0;
	uint8_t *bin = readFile(dir, FW_BIN, &blen);
	uint8_t *txt = readFile(dir, FW_TXT, &tlen);
	uint8_t *clm = readFile(dir, FW_CLM, &clen);
	if (!bin || !txt || !clm) {
		return 1;
	}
	kern_return_t kr = IOConnectCallScalarMethod(conn, kWifiMethodUploadBegin, NULL, 0, NULL, NULL);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "wifi: upload begin: %s\n", errstr(kr));
		lasterror();
		return 1;
	}
	printf("wifi: loading firmware (%zu bytes), NVRAM (%zu), CLM (%zu)\n", blen, tlen, clen);
	if (upload(kWifiBlobFirmware, bin, blen) || upload(kWifiBlobNvram, txt, tlen) ||
	    upload(kWifiBlobClm, clm, clen)) {
		return 1;
	}
	kr = IOConnectCallScalarMethod(conn, kWifiMethodBoot, NULL, 0, NULL, NULL);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "wifi: boot: %s\n", errstr(kr));
		lasterror();
		return 1;
	}
	printf("wifi: firmware running\n");
	return 0;
}

static int
cmdScan(void)
{
	wifi_scan_results *r = calloc(1, sizeof(*r));
	size_t sz = sizeof(*r);
	kern_return_t kr = IOConnectCallStructMethod(conn, kWifiMethodScan, NULL, 0, r, &sz);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "wifi: scan: %s\n", errstr(kr));
		lasterror();
		return 1;
	}
	printf("%-32s  %-17s  %4s  %5s  %s\n", "SSID", "BSSID", "CH", "RSSI", "SECURITY");
	for (uint32_t i = 0; i < r->count && i < kWifiMaxScan; i++) {
		wifi_scan_entry *e = &r->entries[i];
		const char *sec = (e->security & kWifiSecWPA2) ? "WPA2" :
		    (e->security & kWifiSecWPA) ? "WPA" : (e->security & kWifiSecPrivacy) ? "WEP" : "open";
		printf("%-32s  %02x:%02x:%02x:%02x:%02x:%02x  %4u  %5d  %s\n",
		    e->ssid[0] ? e->ssid : "(hidden)",
		    e->bssid[0], e->bssid[1], e->bssid[2], e->bssid[3], e->bssid[4], e->bssid[5],
		    e->channel, e->rssi, sec);
	}
	return 0;
}

static int
readConf(const char *path, wifi_join *j)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "wifi: cannot open %s\n", path);
		return -1;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (strncmp(line, "ssid=", 5) == 0) {
			strlcpy(j->ssid, line + 5, sizeof(j->ssid));
		} else if (strncmp(line, "psk=", 4) == 0) {
			strlcpy(j->passphrase, line + 4, sizeof(j->passphrase));
		}
	}
	fclose(f);
	return j->ssid[0] ? 0 : -1;
}

static int
cmdJoin(int argc, char **argv)
{
	wifi_join j;
	memset(&j, 0, sizeof(j));
	if (argc >= 2 && strcmp(argv[0], "-f") == 0) {
		if (readConf(argv[1], &j) != 0) {
			fprintf(stderr, "wifi: %s has no ssid= line\n", argv[1]);
			return 1;
		}
	} else if (argc >= 1) {
		strlcpy(j.ssid, argv[0], sizeof(j.ssid));
		if (argc >= 2) {
			strlcpy(j.passphrase, argv[1], sizeof(j.passphrase));
		}
	} else {
		fprintf(stderr, "usage: wifi join <ssid> [passphrase] | -f <file>\n");
		return 1;
	}
	kern_return_t kr = IOConnectCallStructMethod(conn, kWifiMethodJoin, &j, sizeof(j), NULL, NULL);
	memset(&j, 0, sizeof(j));
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "wifi: join: %s\n", errstr(kr));
		lasterror();
		return 1;
	}
	printf("wifi: associated\n");
	return 0;
}

static int
cmdStatus(void)
{
	static const char *states[] = { "starting", "waiting for firmware", "loading", "up", "failed" };
	wifi_status st;
	size_t sz = sizeof(st);
	kern_return_t kr = IOConnectCallStructMethod(conn, kWifiMethodStatus, NULL, 0, &st, &sz);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "wifi: status: %s\n", errstr(kr));
		return 1;
	}
	printf("state:     %s\n", st.state < 5 ? states[st.state] : "?");
	printf("chip:      %u rev %u\n", st.chipid, st.chiprev);
	if (st.fwversion[0]) printf("firmware:  %s\n", st.fwversion);
	printf("mac:       %02x:%02x:%02x:%02x:%02x:%02x\n",
	    st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5]);
	if (st.associated) {
		printf("ssid:      %s\n", st.ssid);
		printf("bssid:     %02x:%02x:%02x:%02x:%02x:%02x\n",
		    st.bssid[0], st.bssid[1], st.bssid[2], st.bssid[3], st.bssid[4], st.bssid[5]);
		printf("rssi:      %d dBm\n", st.rssi);
	} else {
		printf("link:      not associated\n");
	}
	if (st.lasterror[0]) printf("error:     %s\n", st.lasterror);
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: wifi up [dir] | scan | join <ssid> [passphrase] | join -f <file> | leave | status\n");
		return 2;
	}
	if (openDriver() != 0) {
		return 1;
	}
	const char *c = argv[1];
	int rc;
	if (strcmp(c, "up") == 0) {
		rc = cmdUp(argc > 2 ? argv[2] : FW_DIR);
	} else if (strcmp(c, "scan") == 0) {
		rc = cmdScan();
	} else if (strcmp(c, "join") == 0) {
		rc = cmdJoin(argc - 2, argv + 2);
	} else if (strcmp(c, "leave") == 0) {
		kern_return_t kr = IOConnectCallScalarMethod(conn, kWifiMethodLeave, NULL, 0, NULL, NULL);
		rc = kr == KERN_SUCCESS ? 0 : 1;
		if (rc) fprintf(stderr, "wifi: leave: %s\n", errstr(kr));
	} else if (strcmp(c, "status") == 0) {
		rc = cmdStatus();
	} else {
		fprintf(stderr, "wifi: unknown command %s\n", c);
		rc = 2;
	}
	IOServiceClose(conn);
	return rc;
}
