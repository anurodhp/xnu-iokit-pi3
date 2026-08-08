/*
 * BCM2837 GPIO function-select, physical (MMU off) access only.
 * Base confirmed against third_party/qemu-zhuowei/include/hw/arm/
 * raspi_platform.h (GPIO_OFFSET=0x200000) + bcm2836.c's PERI_BASE
 * (0x3F000000). Register layout (GPFSEL0-5 at 0x00-0x14, 10 pins per
 * 32-bit register, 3 bits per pin) matches
 * third_party/qemu-zhuowei/hw/gpio/bcm2835_gpio.c's own #defines exactly.
 */
#include "gpio.h"

#define GPIO_BASE 0x3F200000UL
#define GPFSEL(n) (*(volatile uint32_t *)(GPIO_BASE + 0x00 + 4 * (n)))

uint32_t
gpio_get_fsel(unsigned pin)
{
	uint32_t reg = GPFSEL(pin / 10);
	uint32_t shift = (pin % 10) * 3;
	return (reg >> shift) & 0x7;
}

void
gpio_set_fsel(unsigned pin, uint32_t val)
{
	uint32_t regN = pin / 10;
	uint32_t shift = (pin % 10) * 3;
	uint32_t reg = GPFSEL(regN);
	reg &= ~(0x7U << shift);
	reg |= (val & 0x7U) << shift;
	GPFSEL(regN) = reg;
}

void
gpio_set_sd_pins_fsel(uint32_t val)
{
	for (unsigned pin = 48; pin <= 53; pin++) {
		gpio_set_fsel(pin, val);
	}
}
