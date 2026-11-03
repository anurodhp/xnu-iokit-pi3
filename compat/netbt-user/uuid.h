/*
 * NetBSD <uuid.h> (DCE 1.1 uuid API: struct uuid, uuid_from_string, ...)
 * for the netbt userland (libbluetooth's sdp.h, sdpd, sdpquery). Darwin's
 * uuid_t is unsigned char[16] (sys/_types/_uuid_t.h, pulled in by
 * <unistd.h>), so Darwin's is defined first and NetBSD's struct is renamed
 * netbsd_uuid_t for everything that includes this header. Semantics follow
 * NetBSD lib/libc/uuid (uuid_from_string, uuid_to_string, uuid_equal) and
 * sys/kern/kern_uuid.c (uuid_dec_be/uuid_enc_be).
 */
#ifndef _NETBT_USER_UUID_H_
#define _NETBT_USER_UUID_H_
#include <sys/_types/_uuid_t.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define uuid_t netbsd_uuid_t
struct uuid {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_hi_and_version;
	uint8_t clock_seq_hi_and_reserved;
	uint8_t clock_seq_low;
	uint8_t node[6];
};
typedef struct uuid netbsd_uuid_t;

#define uuid_s_ok                       0
#define uuid_s_bad_version              1
#define uuid_s_invalid_string_uuid      2
#define uuid_s_no_memory                3

static __inline void
uuid_dec_be(const void *buf, uuid_t *u)
{
	const uint8_t *p = buf;
	int i;
	u->time_low = (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
	u->time_mid = (uint16_t)(p[4] << 8 | p[5]);
	u->time_hi_and_version = (uint16_t)(p[6] << 8 | p[7]);
	u->clock_seq_hi_and_reserved = p[8];
	u->clock_seq_low = p[9];
	for (i = 0; i < 6; i++)
		u->node[i] = p[10 + i];
}

static __inline void
uuid_enc_be(void *buf, const uuid_t *u)
{
	uint8_t *p = buf;
	int i;
	p[0] = u->time_low >> 24; p[1] = u->time_low >> 16; p[2] = u->time_low >> 8; p[3] = u->time_low;
	p[4] = u->time_mid >> 8; p[5] = u->time_mid;
	p[6] = u->time_hi_and_version >> 8; p[7] = u->time_hi_and_version;
	p[8] = u->clock_seq_hi_and_reserved;
	p[9] = u->clock_seq_low;
	for (i = 0; i < 6; i++)
		p[10 + i] = u->node[i];
}

static __inline int32_t
uuid_equal(const uuid_t *a, const uuid_t *b, uint32_t *status)
{
	if (status != NULL)
		*status = uuid_s_ok;
	if (a == NULL || b == NULL) {
		uuid_t z;
		memset(&z, 0, sizeof(z));
		return memcmp(a ? a : &z, b ? b : &z, sizeof(z)) == 0;
	}
	return memcmp(a, b, sizeof(*a)) == 0;
}

static __inline void
uuid_from_string(const char *s, uuid_t *u, uint32_t *status)
{
	unsigned int c[11];
	int n, i;

	if (s == NULL || *s == '\0') {
		memset(u, 0, sizeof(*u));
		if (status != NULL) *status = uuid_s_ok;
		return;
	}
	n = sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
	    &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8], &c[9], &c[10]);
	if (n != 11 || strlen(s) != 36) {
		if (status != NULL) *status = uuid_s_invalid_string_uuid;
		return;
	}
	u->time_low = c[0];
	u->time_mid = (uint16_t)c[1];
	u->time_hi_and_version = (uint16_t)c[2];
	u->clock_seq_hi_and_reserved = (uint8_t)c[3];
	u->clock_seq_low = (uint8_t)c[4];
	for (i = 0; i < 6; i++)
		u->node[i] = (uint8_t)c[5 + i];
	if (status != NULL) *status = uuid_s_ok;
}

static __inline void
uuid_to_string(const uuid_t *u, char **s, uint32_t *status)
{
	char *b = malloc(37);
	if (b == NULL) {
		if (status != NULL) *status = uuid_s_no_memory;
		return;
	}
	snprintf(b, 37, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	    u->time_low, u->time_mid, u->time_hi_and_version,
	    u->clock_seq_hi_and_reserved, u->clock_seq_low,
	    u->node[0], u->node[1], u->node[2], u->node[3], u->node[4], u->node[5]);
	*s = b;
	if (status != NULL) *status = uuid_s_ok;
}
#endif
