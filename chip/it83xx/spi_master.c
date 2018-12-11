/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI module for Chrome EC */

#include "clock.h"
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
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)

enum sspi_clk_sel {
	sspi_clk_24mhz = 0,
	sspi_clk_12mhz,
	sspi_clk_8mhz,
	sspi_clk_6mhz,
	sspi_clk_4p8mhz,
	sspi_clk_4mhz,
	sspi_clk_3p428mhz,
	sspi_clk_3mhz,
};

enum sspi_ch_sel {
	SSPI_CH_CS0 = 0,
	SSPI_CH_CS1,
};

static void sspi_frequency(enum sspi_clk_sel freq)
{
	/*
	 * bit[6:5]
	 * Bit 6:Clock Polarity (CLPOL)
	 * 0: SSCK is low in the idle mode.
	 * 1: SSCK is high in the idle mode.
	 * Bit 5:Clock Phase (CLPHS)
	 * 0: Latch data on the first SSCK edge.
	 * 1: Latch data on the second SSCK edge.
	 *
	 * bit[4:2]
	 * 000b: 1/2 clk_sspi
	 * 001b: 1/4 clk_sspi
	 * 010b: 1/6 clk_sspi
	 * 011b: 1/8 clk_sspi
	 * 100b: 1/10 clk_sspi
	 * 101b: 1/12 clk_sspi
	 * 110b: 1/14 clk_sspi
	 * 111b: 1/16 clk_sspi
	 *
	 * SSCK frequency is [freq] MHz and mode 3.
	 * note, clk_sspi need equal to 48MHz above.
	 */
	IT83XX_SSPI_SPICTRL1 |= (0x60 | (freq << 2));
}

static void sspi_transmission_end(void)
{
	/* Write 1 to end the SPI transmission. */
	IT83XX_SSPI_SPISTS = 0x20;

	/* Short delay for "Transfer End Flag" */
	IT83XX_GCTRL_WNCKR = 0;

	/* Write 1 to clear this bit and terminate data transmission. */
	IT83XX_SSPI_SPISTS = 0x02;
}

/* We assume only one SPI port in the chip, one SPI device */
int spi_enable(int port, int enable)
{
	if (enable) {
		/*
		 * bit[5:4]
		 * 00b: SPI channel 0 and channel 1 are disabled.
		 * 10b: SSCK/SMOSI/SMISO/SSCE1# are enabled.
		 * 01b: SSCK/SMOSI/SMISO/SSCE0# are enabled.
		 * 11b: SSCK/SMOSI/SMISO/SSCE1#/SSCE0# are enabled.
		 */
		if (port == SSPI_CH_CS1)
			IT83XX_GPIO_GRC1 |= 0x20;
		else
			IT83XX_GPIO_GRC1 |= 0x10;

		gpio_config_module(MODULE_SPI, 1);
	} else {
		if (port == SSPI_CH_CS1)
			IT83XX_GPIO_GRC1 &= ~0x20;
		else
			IT83XX_GPIO_GRC1 &= ~0x10;

		gpio_config_module(MODULE_SPI, 0);
	}

	return EC_SUCCESS;
}

int spi_transaction(const struct spi_device_t *spi_device,
		const uint8_t *txdata, int txlen,
		uint8_t *rxdata, int rxlen)
{
	int idx;
	uint8_t port = spi_device->port;
	static struct mutex spi_mutex;

	mutex_lock(&spi_mutex);
	/* bit[0]: Write cycle */
	IT83XX_SSPI_SPICTRL2 &= ~0x04;
	for (idx = 0x00; idx < txlen; idx++) {
		IT83XX_SSPI_SPIDATA = txdata[idx];
		if (port == SSPI_CH_CS1)
			/* Write 1 to start the data transmission of CS1 */
			IT83XX_SSPI_SPISTS |= 0x08;
		else
			/* Write 1 to start the data transmission of CS0 */
			IT83XX_SSPI_SPISTS |= 0x10;
	}

	/* bit[1]: Read cycle */
	IT83XX_SSPI_SPICTRL2 |= 0x04;
	for (idx = 0x00; idx < rxlen; idx++) {
		if (port == SSPI_CH_CS1)
			/* Write 1 to start the data transmission of CS1 */
			IT83XX_SSPI_SPISTS |= 0x08;
		else
			/* Write 1 to start the data transmission of CS0 */
			IT83XX_SSPI_SPISTS |= 0x10;
		rxdata[idx] = IT83XX_SSPI_SPIDATA;
	}

	sspi_transmission_end();
	mutex_unlock(&spi_mutex);

	return EC_SUCCESS;
}

static void sspi_init(void)
{
	int i;

	clock_enable_peripheral(CGC_OFFSET_SSPI, 0, 0);
	sspi_frequency(sspi_clk_8mhz);

	/*
	 * bit[5:3] Byte Width (BYTEWIDTH)
	 * 000b: 8-bit transmission
	 * 001b: 1-bit transmission
	 * 010b: 2-bit transmission
	 * 011b: 3-bit transmission
	 * 100b: 4-bit transmission
	 * 101b: 5-bit transmission
	 * 110b: 6-bit transmission
	 * 111b: 7-bit transmission
	 *
	 * bit[1] Blocking selection
	 */
	IT83XX_SSPI_SPICTRL2 |= 0x02;

	for (i = 0; i < spi_devices_used; i++)
		/* Disabling spi module */
		spi_enable(spi_devices[i].port, 0);
}
DECLARE_HOOK(HOOK_INIT, sspi_init, HOOK_PRIO_INIT_SPI);
