/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_STREAM_SIGNATURE
#include "signing.h"
#endif

/* Not defined in the hardware register spec, the RX and TX buffers are 128B. */
#define SPI_BUF_SIZE 0x80

/* This timeout should allow a full buffer transaction at the lowest SPI speed
 * by using the largest uint8_t clock divider of 256 (~235kHz). */
#define SPI_TRANSACTION_TIMEOUT_USEC (5 * MSEC)

/* There are two SPI masters or ports on this chip. */
#define SPI_NUM_PORTS 2

static struct mutex spi_mutex[SPI_NUM_PORTS];
static enum spi_clock_mode clock_mode[SPI_NUM_PORTS];

/* The Cr50 SPI master is not DMA auto-fill/drain capable, so async and flush
 * are not defined on purpose. */
int spi_transaction(const struct spi_device_t *spi_device,
		    const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int port = spi_device->port;
	int rv = EC_SUCCESS;
	timestamp_t timeout;
	int transaction_size = 0;
	int rxoffset = 0;

	/* If SPI0's passthrough is enabled, SPI0 is not available unless the
	 * SPS's BUSY bit is set. */
	if (port == 0) {
		if (GREAD_FIELD_I(SPI, port, CTRL, ENPASSTHRU) &&
		    !GREAD(SPS, EEPROM_BUSY_STATUS))
			return EC_ERROR_BUSY;
	}

	if (rxlen == SPI_READBACK_ALL) {
		/* Bidirectional SPI sends and receives a bit for each clock.
		 * We'll need to make sure the buffers for RX and TX are equal
		 * and return a bit received for every bit sent.
		 */
		if (txlen > SPI_BUF_SIZE)
			return EC_ERROR_INVAL;
		rxlen = txlen;
		transaction_size = txlen;
		rxoffset = 0;
	} else {
		/* Ensure it'll fit inside of the RX and TX buffers. Note that
		 * although the buffers are separate, the total transmission
		 * size must fit in the rx buffer.
		 */
		if (txlen + rxlen > SPI_BUF_SIZE)
			return EC_ERROR_INVAL;
		transaction_size = rxlen + txlen;
		rxoffset = txlen;
	}

	/* Grab the port's mutex. */
	mutex_lock(&spi_mutex[port]);

#ifdef CONFIG_STREAM_SIGNATURE
	/*
	 * This hook allows mn50 to sniff data written to target
	 * manufactured H1 devices.
	 */
	sig_append(stream_spiflash, txdata, txlen);
#endif

	/* Copy the txdata into the 128B Transmit Buffer. */
	memmove((uint8_t *)GREG32_ADDR_I(SPI, port, TX_DATA), txdata, txlen);

#ifndef CONFIG_SPI_MASTER_NO_CS_GPIOS
	/* Drive chip select low. */
	gpio_set_level(spi_device->gpio_cs, 0);
#endif  /* CONFIG_SPI_MASTER_NO_CS_GPIOS */

	/* Initiate the transaction. */
	GWRITE_FIELD_I(SPI, port, ISTATE_CLR, TXDONE, 1);
	GWRITE_FIELD_I(SPI, port, XACT, SIZE, transaction_size - 1);
	GWRITE_FIELD_I(SPI, port, XACT, START, 1);

	/* Wait for the SPI master to finish the transaction. */
	timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
	while (!GREAD_FIELD_I(SPI, port, ISTATE, TXDONE)) {
		/* Give up if the deadline has been exceeded. */
		if (get_time().val > timeout.val) {
			/* Might have been pre-empted by other task.
			 * Check ISTATE.TXDONE again for legit timeout.
			 */
			if (GREAD_FIELD_I(SPI, port, ISTATE, TXDONE))
				break;
			rv = EC_ERROR_TIMEOUT;
			goto err_cs_high;
		}
	}
	GWRITE_FIELD_I(SPI, port, ISTATE_CLR, TXDONE, 1);

	/* Copy the result. */
	memmove(rxdata,
		&((uint8_t *)GREG32_ADDR_I(SPI, port, RX_DATA))[rxoffset],
		rxlen);

err_cs_high:
#ifndef CONFIG_SPI_MASTER_NO_CS_GPIOS
	/* Drive chip select high. */
	gpio_set_level(spi_device->gpio_cs, 1);
#endif  /* CONFIG_SPI_MASTER_NO_CS_GPIOS */

	/* Release the port's mutex. */
	mutex_unlock(&spi_mutex[port]);
	return rv;
}

/*
 * Configure the SPI port's clock mode. The SPI port must be re-enabled after
 * changing the clocking mode.
 */
void set_spi_clock_mode(int port, enum spi_clock_mode mode)
{
	clock_mode[port] = mode;
}

/*
 * Configure the SPI0 master's passthrough mode. Note:
 * 1) This must be called after the SPI port is enabled.
 * 2) Passthrough cannot be safely disabled while the SPI slave port is active
 *    and the SPI slave port's status register's BUSY bit is not set.
 */
void configure_spi0_passthrough(int enable)
{
	int port = 0;

	/* Grab the port's mutex. */
	mutex_lock(&spi_mutex[port]);

	GWRITE_FIELD_I(SPI, port, CTRL, ENPASSTHRU, enable);

	/* Release the port's mutex. */
	mutex_unlock(&spi_mutex[port]);
}

int spi_enable(int port, int enable)
{
	int i;

	if (enable) {
		int spi_device_found = 0;
		uint8_t max_div = 0;

#ifndef CONFIG_SPI_MASTER_NO_CS_GPIOS
		gpio_config_module(MODULE_SPI, 1);
#endif  /* CONFIG_SPI_MASTER_NO_CS_GPIOS */
		for (i = 0; i < spi_devices_used; i++) {
			if (spi_devices[i].port != port)
				continue;

			spi_device_found = 1;

#ifndef CONFIG_SPI_MASTER_NO_CS_GPIOS
			/* Deassert CS# */
			gpio_set_flags(spi_devices[i].gpio_cs, GPIO_OUTPUT);
			gpio_set_level(spi_devices[i].gpio_cs, 1);
#endif  /* CONFIG_SPI_MASTER_NO_CS_GPIOS */

			/* Find the port's largest DIV (lowest frequency). */
			if (spi_devices[i].div > max_div)
				max_div = spi_devices[i].div;
		}

		/* Ensure there is at least one device behind the SPI port. */
		if (!spi_device_found)
			return EC_ERROR_INVAL;

		/* configure the SPI clock mode */
		GWRITE_FIELD_I(SPI, port, CTRL, CPOL,
			       (clock_mode[port] == SPI_CLOCK_MODE2) ||
			       (clock_mode[port] == SPI_CLOCK_MODE3));
		GWRITE_FIELD_I(SPI, port, CTRL, CPHA,
			       (clock_mode[port] == SPI_CLOCK_MODE1) ||
			       (clock_mode[port] == SPI_CLOCK_MODE3));

		/* Enforce the default setup and hold times. */
		GWRITE_FIELD_I(SPI, port, CTRL, CSBSU, 0);
		GWRITE_FIELD_I(SPI, port, CTRL, CSBHLD, 0);

		/* Set the clock divider, where freq / (div + 1). */
		GWRITE_FIELD_I(SPI, port, CTRL, IDIV, max_div);

		/* Master's CS is active low. */
		GWRITE_FIELD_I(SPI, port, CTRL, CSBPOL, 0);

		/* Byte 0 bit 7 is first in each double word in the buffers. */
		GWRITE_FIELD_I(SPI, port, CTRL, TXBITOR, 1);
		GWRITE_FIELD_I(SPI, port, CTRL, TXBYTOR, 0);
		GWRITE_FIELD_I(SPI, port, CTRL, RXBITOR, 1);
		GWRITE_FIELD_I(SPI, port, CTRL, RXBYTOR, 0);

		/* Disable passthrough by default. */
		if (port == 0)
			configure_spi0_passthrough(0);

		/* Disable the TXDONE interrupt, we'll busy poll instead. */
		GWRITE_FIELD_I(SPI, port, ICTRL, TXDONE, 0);

	} else {
		for (i = 0; i < spi_devices_used; i++) {
			if (spi_devices[i].port != port)
				continue;

#ifndef CONFIG_SPI_MASTER_NO_CS_GPIOS
			/* Make sure CS# is deasserted and disabled. */
			gpio_set_level(spi_devices[i].gpio_cs, 1);
			gpio_set_flags(spi_devices[i].gpio_cs, GPIO_ODR_HIGH);
#endif  /* CONFIG_SPI_MASTER_NO_CS_GPIOS */
		}

		/* Disable passthrough. */
		if (port == 0)
			configure_spi0_passthrough(0);

		gpio_config_module(MODULE_SPI, 1);
	}

	return EC_SUCCESS;
}

/******************************************************************************/
/* Hooks */

static void spi_init(void)
{
	size_t i;

#ifdef CONFIG_SPI_MASTER_CONFIGURE_GPIOS
	/* Set SPI_MISO as an input */
	GWRITE_FIELD(PINMUX, DIOA11_CTL, IE, 1); /* SPI_MISO */
#endif

	for (i = 0; i < SPI_NUM_PORTS; i++) {
		/* Configure the SPI ports to default to mode0. */
		set_spi_clock_mode(i, SPI_CLOCK_MODE0);

		/* Ensure the SPI ports are disabled to prevent us from
		 * interfering with the main chipset when we're not explicitly
		 * using the SPI bus. */
		spi_enable(i, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT);
