#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex32(uint32_t v);
void uart_hex64(uint64_t v);
void uart_dec(uint32_t v);
void uart_hexdump(const void *buf, uint32_t len);

#endif
