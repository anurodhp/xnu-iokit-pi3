/*
 * snprintb(3) for the netbt userland (btconfig's flag display), the
 * old-style bit format of NetBSD lib/libutil/snprintb.c: the first byte is
 * the output base (\10 octal, \20 hex), then for each named bit a byte with
 * the bit number (1 = least significant) followed by the name, which ends
 * at the next byte <= ' '. Prints e.g. "0x5<UP,XMIT_CMD>". New-style
 * formats (first byte \177) are not used by any tool built here and print
 * the bare number.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int
snprintb(char *buf, size_t len, const char *fmt, uint64_t val)
{
	size_t n;
	int any = 0;
	const unsigned char *p = (const unsigned char *)fmt;

	if (len == 0)
		return 0;
	if (*p == 8)
		n = (size_t)snprintf(buf, len, "%#llo", (unsigned long long)val);
	else
		n = (size_t)snprintf(buf, len, "%#llx", (unsigned long long)val);
	if (*p != 8 && *p != 16)
		return (int)n;
	for (p++; *p != '\0' && n < len; ) {
		unsigned bit = *p++;
		const unsigned char *name = p;
		while (*p > ' ')
			p++;
		if (bit >= 1 && bit <= 64 && (val & ((uint64_t)1 << (bit - 1)))) {
			n += (size_t)snprintf(buf + n, len - n, "%c%.*s", any ? ',' : '<',
			    (int)(p - name), (const char *)name);
			any = 1;
		}
	}
	if (any && n < len)
		n += (size_t)snprintf(buf + n, len - n, ">");
	return (int)n;
}
