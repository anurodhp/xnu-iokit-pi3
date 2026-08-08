#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

/* Same encoding as third_party/xnu/pexpert/pexpert/arm64/BCM2837.h's
 * BCM2837_FSEL_* (0=input,1=output,4=ALT0,5=ALT1,6=ALT2,7=ALT3,3=ALT4,2=ALT5),
 * confirmed against the real BCM2835/2837 GPFSEL 3-bit field encoding. */
#define GPIO_FSEL_INPUT 0x0
#define GPIO_FSEL_ALT0  0x4
#define GPIO_FSEL_ALT3  0x7

uint32_t gpio_get_fsel(unsigned pin);
void gpio_set_fsel(unsigned pin, uint32_t val);
void gpio_set_sd_pins_fsel(uint32_t val); /* pins 48-53, one call each */

#endif
