/*
 * Minimal polled PL011 (UART0) driver, physical (MMU off) access only.
 * Base address confirmed against this project's own QEMU fork source
 * (third_party/qemu-zhuowei/include/hw/arm/raspi_platform.h:
 * UART0_OFFSET=0x201000, on top of bcm2836.c's BCM2836_PERI_BASE=0x3F000000).
 *
 * QEMU's pl011_write() (hw/char/pl011.c) transmits on a DR write
 * unconditionally - "??? Check if transmitter is enabled" is a literal
 * TODO left unimplemented in their model - so no CR/LCR_H setup is
 * strictly required. Still done here for hygiene/portability.
 */
#include "uart.h"

#define UART0_BASE 0x3F201000UL

#define UART0_DR   (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART0_FR   (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART0_IBRD (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART0_FBRD (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART0_LCRH (*(volatile uint32_t *)(UART0_BASE + 0x2C))
#define UART0_CR   (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define UART0_ICR  (*(volatile uint32_t *)(UART0_BASE + 0x44))

#define FR_TXFF 0x20

void
uart_init(void)
{
	UART0_CR = 0;
	UART0_ICR = 0x7FF;
	UART0_IBRD = 1;
	UART0_FBRD = 40;
	UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6); /* FIFO enable, 8N1 */
	UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);   /* UARTEN, TXE, RXE */
}

void
uart_putc(char c)
{
	if (c == '\n') {
		uart_putc('\r');
	}
	while (UART0_FR & FR_TXFF) {
		;
	}
	UART0_DR = (uint32_t)(unsigned char)c;
}

void
uart_puts(const char *s)
{
	while (*s) {
		uart_putc(*s++);
	}
}

static void
uart_hex_nibbles(uint64_t v, int nibbles)
{
	uart_puts("0x");
	for (int i = (nibbles - 1) * 4; i >= 0; i -= 4) {
		uint32_t nib = (uint32_t)((v >> i) & 0xF);
		uart_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
	}
}

void
uart_hex32(uint32_t v)
{
	uart_hex_nibbles(v, 8);
}

void
uart_hex64(uint64_t v)
{
	uart_hex_nibbles(v, 16);
}

void
uart_dec(uint32_t v)
{
	char buf[11];
	int i = 10;
	buf[10] = '\0';
	if (v == 0) {
		uart_putc('0');
		return;
	}
	while (v > 0 && i > 0) {
		buf[--i] = (char)('0' + (v % 10));
		v /= 10;
	}
	uart_puts(&buf[i]);
}

void
uart_hexdump(const void *buf, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	for (uint32_t i = 0; i < len; i++) {
		if (i != 0 && (i % 16) == 0) {
			uart_putc('\n');
		}
		uint8_t b = p[i];
		uart_putc("0123456789abcdef"[b >> 4]);
		uart_putc("0123456789abcdef"[b & 0xF]);
		uart_putc(' ');
	}
	uart_putc('\n');
}
