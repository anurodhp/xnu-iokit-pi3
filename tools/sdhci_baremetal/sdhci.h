#ifndef SDHCI_H
#define SDHCI_H

#include <stdint.h>

typedef struct {
	int      card_present;
	int      card_is_sdhc;
	uint32_t rca;
	uint64_t block_count;
} sdhci_state_t;

/* All operate against the Arasan/eMMC-style SDHCI controller at physical
 * 0x3F300000 (VC offset 0x300000) - same controller
 * iokit/BCM2837SDHCI targets, not the "sdhost" controller. Register
 * offsets/command sequence are a direct freestanding port of that kext's
 * logic (resetHost/setClock/sendCommand/cardIdentify/readBlocksPIO in
 * BCM2837SDHCI.cpp), swapping IOLog->uart, IODelay/interrupt-wait->busy
 * poll, since there's no IOKit/interrupt controller here. */
int  sdhci_reset(void);
int  sdhci_set_clock(void);
void sdhci_power_on(void); /* SDHCI_POWER_CONTROL: 3.3V | SD_BUS_POWER, matches kext's start() */
int  sdhci_card_identify(sdhci_state_t *st);
int  sdhci_read_block(sdhci_state_t *st, uint64_t block, void *buf512);

#endif
