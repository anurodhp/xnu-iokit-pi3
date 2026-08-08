/*
 * Orchestrates three tests against QEMU's emulated Arasan SDHCI controller
 * and GPIO48-53 mux - see README.md for the full writeup. RESULT: lines
 * are meant to be grepped straight out of the serial log.
 */
#include "uart.h"
#include "gpio.h"
#include "sdhci.h"

static int
check_pattern(const uint8_t *buf)
{
	for (uint32_t i = 0; i < 512; i++) {
		if (buf[i] != (uint8_t)i) {
			uart_puts("  mismatch at offset ");
			uart_dec(i);
			uart_puts(": got ");
			uart_hex32(buf[i]);
			uart_puts(" want ");
			uart_hex32((uint8_t)i);
			uart_puts("\n");
			return 0;
		}
	}
	return 1;
}

static int
run_full_sequence(const char *label, uint8_t *blockbuf, int expect_success)
{
	sdhci_state_t st;
	int ok = sdhci_reset() && sdhci_set_clock();
	if (ok) {
		sdhci_power_on();
		ok = sdhci_card_identify(&st);
	}

	if (!ok) {
		uart_puts("RESULT: ");
		uart_puts(label);
		uart_puts(expect_success ? " FAIL (expected card, got none)\n" : " PASS (no card, as expected)\n");
		return !expect_success;
	}

	if (!expect_success) {
		uart_puts("RESULT: ");
		uart_puts(label);
		uart_puts(" FAIL (card identified but none was expected)\n");
		return 0;
	}

	int readok = sdhci_read_block(&st, 0, blockbuf);
	if (!readok) {
		uart_puts("RESULT: ");
		uart_puts(label);
		uart_puts(" FAIL (card identified but block-0 read failed)\n");
		return 0;
	}

	uart_puts("block 0 first 32 bytes:\n");
	uart_hexdump(blockbuf, 32);

	int match = check_pattern(blockbuf);
	uart_puts("RESULT: ");
	uart_puts(label);
	uart_puts(match ? " PASS (card read, data matches expected pattern)\n"
	                 : " FAIL (card read, data MISMATCH)\n");
	return match;
}

int
main(void)
{
	static uint8_t blockbuf[512];

	uart_init();
	uart_puts("\n=== sdhci_baremetal: BCM2837 GPIO mux + Arasan SDHCI test ===\n");

	uart_puts("GPIO fsel[48..53] at boot: ");
	for (unsigned pin = 48; pin <= 53; pin++) {
		uart_hex32(gpio_get_fsel(pin));
		uart_puts(" ");
	}
	uart_puts("\n");

	uart_puts("\n--- Test A: default routing, no GPIO write ---\n");
	int a = run_full_sequence("TestA(default)", blockbuf, 1);

	uart_puts("\n--- Test B: negative control, write ALT0(4) to GPIO48-53 ---\n");
	gpio_set_sd_pins_fsel(GPIO_FSEL_ALT0);
	uart_puts("GPIO fsel[48..53] now: ");
	for (unsigned pin = 48; pin <= 53; pin++) {
		uart_hex32(gpio_get_fsel(pin));
		uart_puts(" ");
	}
	uart_puts("\n");
	int b = run_full_sequence("TestB(alt0)", blockbuf, 0);

	uart_puts("\n--- Test C: restore, write INPUT(0) back to GPIO48-53 ---\n");
	gpio_set_sd_pins_fsel(GPIO_FSEL_INPUT);
	uart_puts("GPIO fsel[48..53] now: ");
	for (unsigned pin = 48; pin <= 53; pin++) {
		uart_hex32(gpio_get_fsel(pin));
		uart_puts(" ");
	}
	uart_puts("\n");
	int c = run_full_sequence("TestC(restored)", blockbuf, 1);

	uart_puts("\n=== SUMMARY: A=");
	uart_puts(a ? "PASS" : "FAIL");
	uart_puts(" B=");
	uart_puts(b ? "PASS" : "FAIL");
	uart_puts(" C=");
	uart_puts(c ? "PASS" : "FAIL");
	uart_puts(" ===\n");
	uart_puts(a && b && c ? "OVERALL: PASS\n" : "OVERALL: FAIL\n");

	for (;;) {
		;
	}
	return 0;
}
