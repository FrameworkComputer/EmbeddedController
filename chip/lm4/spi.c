/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI module for Chrome EC */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTF(format, args...) cprintf(CC_SPI, format, ## args)


int spi_enable(int enable)
{
	if (enable) {
		gpio_config_module(MODULE_SPI, 1);
		/* Don't use the SSI0 frame output.  CS# is a GPIO so we can
		 * keep it low during an entire transaction. */
		gpio_set_flags(GPIO_SPI_CSn, GPIO_OUTPUT);
		gpio_set_level(GPIO_SPI_CSn, 1);

		/* Enable SSI port */
		LM4_SSI_CR1(0) |= 0x02;
	} else {
		/* Disable SSI port */
		LM4_SSI_CR1(0) &= ~0x02;

		/* Make sure CS# is deselected */
		gpio_set_level(GPIO_SPI_CSn, 1);
		gpio_set_flags(GPIO_SPI_CSn, GPIO_ODR_HIGH);

		gpio_config_module(MODULE_SPI, 0);
	}

	return EC_SUCCESS;
}


int spi_transaction(const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int totallen = txlen + rxlen;
	int txcount = 0, rxcount = 0;
	volatile uint32_t dummy  __attribute__((unused));

	/* Empty the receive FIFO */
	while (LM4_SSI_SR(0) & LM4_SSI_SR_RNE)
		dummy = LM4_SSI_DR(0);

	/* Start transaction.  Need to do this explicitly because the LM4
	 * SSI controller pulses its frame select every byte, and the EEPROM
	 * wants the chip select held low during the entire transaction. */
	gpio_set_level(GPIO_SPI_CSn, 0);

	while (rxcount < totallen) {
		/* Handle received bytes if any.  We just checked rxcount <
		 * totallen, so we don't need to worry about overflowing the
		 * receive buffer. */
		if (LM4_SSI_SR(0) & LM4_SSI_SR_RNE) {
			if (rxcount < txlen) {
				/* Throw away bytes received while we were
				   transmitting */
				dummy = LM4_SSI_DR(0);
			} else
				*(rxdata++) = LM4_SSI_DR(0);
			rxcount++;
		}

		/* Transmit another byte if needed */
		if ((LM4_SSI_SR(0) & LM4_SSI_SR_TNF) && txcount < totallen) {
			if (txcount < txlen)
				LM4_SSI_DR(0) = *(txdata++);
			else {
				/* Clock out dummy byte so we can clock in the
				 * response byte */
				LM4_SSI_DR(0) = 0;
			}
			txcount++;
		}
	}

	/* End transaction */
	gpio_set_level(GPIO_SPI_CSn, 1);

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Hooks */

static int spi_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable the SPI module and delay a few clocks */
	LM4_SYSTEM_RCGCSSI = 1;
	scratch = LM4_SYSTEM_RCGCSSI;

	LM4_SSI_CR1(0) = 0;       /* Disable SSI */
	LM4_SSI_CR0(0) = 0x0007;  /* SCR=0, SPH=0, SPO=0, FRF=SPI, 8-bit */

	/* Use PIOSC for clock.  This limits us to 8MHz (PIOSC/2), but is
	 * simpler to configure and we don't need to worry about clock
	 * frequency changing when the PLL is disabled.  If we really start
	 * using this, might be worth using the system clock and handling
	 * frequency change (like we do with PECI) so we can go faster. */
	LM4_SSI_CC(0) = 1;
	/* SSICLK = PIOSC / (CPSDVSR * (1 + SCR)
	 *        = 16 MHz / (2 * (1 + 0))
	 *        = 8 MHz */
	LM4_SSI_CPSR(0) = 2;

	/* Ensure the SPI port is disabled.  This keeps us from interfering
	 * with the main chipset when we're not explicitly using the SPI
	 * bus. */
	spi_enable(0);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

static int printrx(const char *desc, const uint8_t *txdata, int txlen,
		   int rxlen)
{
	uint8_t rxdata[32];
	int rv;
	int i;

	rv = spi_transaction(txdata, txlen, rxdata, rxlen);
	if (rv)
		return rv;

	ccprintf("%-12s:", desc);
	for (i = 0; i < rxlen; i++)
		ccprintf(" 0x%02x", rxdata[i]);
	ccputs("\n");
	return EC_SUCCESS;
}


static int command_spirom(int argc, char **argv)
{
	uint8_t txmandev[] = {0x90, 0x00, 0x00, 0x00};
	uint8_t txjedec[] = {0x9f};
	uint8_t txunique[] = {0x4b, 0x00, 0x00, 0x00, 0x00};
	uint8_t txsr1[] = {0x05};
	uint8_t txsr2[] = {0x35};

	spi_enable(1);

	printrx("Man/Dev ID", txmandev, sizeof(txmandev), 2);
	printrx("JEDEC ID", txjedec, sizeof(txjedec), 3);
	printrx("Unique ID", txunique, sizeof(txunique), 8);
	printrx("Status reg 1", txsr1, sizeof(txsr1), 1);
	printrx("Status reg 2", txsr2, sizeof(txsr2), 1);

	spi_enable(0);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(spirom, command_spirom,
			NULL,
			"Test reading SPI EEPROM",
			NULL);
