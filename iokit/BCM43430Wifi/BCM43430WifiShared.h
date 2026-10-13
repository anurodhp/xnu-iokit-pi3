/*
 * User-client ABI between BCM43430Wifi (kernel) and /usr/sbin/wifi
 * (tools/userland_staging/wifi.c). Plain C, fixed-size structs, no
 * pointers, so both sides compile it unchanged.
 */
#ifndef _BCM43430WIFISHARED_H
#define _BCM43430WIFISHARED_H

#include <stdint.h>

#define kBCM43430WifiClass "BCM43430Wifi"

enum {
	kWifiMethodUploadBegin = 0,  // no args: halt the chip's CPU, start a firmware upload
	kWifiMethodUploadChunk = 1,  // scalar in: type, offset; struct in: <= kWifiChunkMax bytes
	kWifiMethodBoot        = 2,  // no args: NVRAM, start CPU, CLM, init; attaches the interface
	kWifiMethodScan        = 3,  // struct out: wifi_scan_results (blocks up to ~10 s)
	kWifiMethodJoin        = 4,  // struct in: wifi_join (blocks up to ~20 s); kIOReturnNotPermitted = WPA2 handshake failed (wrong passphrase)
	kWifiMethodLeave       = 5,  // no args
	kWifiMethodStatus      = 6,  // struct out: wifi_status
	kWifiMethodPower       = 7,  // scalar in: 1 = radio on (WLC_UP), 0 = off (disassociate, WLC_DOWN)
	kWifiMethodCount
};

enum {
	kWifiBlobFirmware = 0,
	kWifiBlobNvram    = 1,
	kWifiBlobClm      = 2,
};

#define kWifiChunkMax 4096

enum {
	kWifiStateNone    = 0,  // driver started, chip not probed yet
	kWifiStateProbed  = 1,  // SDIO up, chip identified, waiting for firmware
	kWifiStateLoading = 2,  // firmware upload in progress
	kWifiStateUp      = 3,  // firmware running
	kWifiStateFailed  = 4,
};

enum {
	kWifiSecPrivacy = 1 << 0,  // capability privacy bit
	kWifiSecWPA2    = 1 << 1,  // RSN IE present
	kWifiSecWPA     = 1 << 2,  // WPA vendor IE present
};

typedef struct {
	char     ssid[33];
	uint8_t  bssid[6];
	uint8_t  channel;
	int16_t  rssi;
	uint16_t security;
} wifi_scan_entry;

#define kWifiMaxScan 64

typedef struct {
	uint32_t count;
	wifi_scan_entry entries[kWifiMaxScan];
} wifi_scan_results;

typedef struct {
	char ssid[33];
	char passphrase[65];   // empty = open network; 8-63 chars = WPA2-PSK passphrase; 64 hex = PSK
} wifi_join;

typedef struct {
	uint32_t state;        // kWifiState*
	uint32_t associated;   // 1 when the link is up (and keyed, for WPA2)
	char     ssid[33];
	uint8_t  bssid[6];
	uint8_t  mac[6];
	int32_t  rssi;
	uint32_t chipid;
	uint32_t chiprev;
	char     fwversion[128];
	char     lasterror[96];
	uint32_t radio_off;    // 1 after Power(0): firmware loaded, radio down
	// The rest are filled only while associated (GET_BSS_INFO, GET_RATE).
	uint32_t channel;      // control channel (all 2.4 GHz on the 43430)
	int32_t  noise;        // dBm, 0 if unknown
	uint32_t rate_kbps;    // current transmit rate
	uint32_t ht;           // 1 if the BSS is 802.11n capable
	uint32_t security;     // kWifiSecWPA2 for a WPA2-PSK join, 0 for an open one
} wifi_status;

#endif /* _BCM43430WIFISHARED_H */
