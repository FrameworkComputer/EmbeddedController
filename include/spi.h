/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI interface for Chrome EC */

#ifndef __CROS_EC_SPI_H
#define __CROS_EC_SPI_H

/*
 * SPI Clock polarity and phase mode (0 - 3)
 * @code
 * clk mode | POL PHA
 * ---------+--------
 *   0      |  0   0
 *   1      |  0   1
 *   2      |  1   0
 *   3      |  1   1
 * ---------+--------
 * @endcode
 */
enum spi_clock_mode {
	SPI_CLOCK_MODE0 = 0,
	SPI_CLOCK_MODE1 = 1,
	SPI_CLOCK_MODE2 = 2,
	SPI_CLOCK_MODE3 = 3
};

struct spi_device_t {
	/*
	 * SPI port the device is connected to.
	 * On some architecture, this is SPI master port index,
	 * on other the SPI port index directly.
	 */
	uint8_t port;

	/*
	 * Clock divisor to talk to SPI device.
	 * If several devices share the same port, we select the lowest speed.
	 */
	uint8_t div;

	/* gpio used for chip selection. */
	enum gpio_signal gpio_cs;
};

extern const struct spi_device_t spi_devices[];
extern const unsigned int spi_devices_used;

/*
 * The first port in spi_ports define the port to access the SPI flash.
 * The first gpio defines the CS GPIO to access the flash,
 * if used.
 */
#define SPI_FLASH_DEVICE (&spi_devices[0])

/*
 * Enable / disable the SPI port.  When the port is disabled, all its I/O lines
 * are high-Z so the EC won't interfere with other devices on the SPI bus.
 *
 * @param port  port id to work on.
 * @param enable  1 to enable the port, 0 to disable it.
 */
int spi_enable(int port, int enable);

/* Issue a SPI transaction.  Assumes SPI port has already been enabled.
 *
 * Transmits <txlen> bytes from <txdata>, throwing away the corresponding
 * received data, then transmits <rxlen> dummy bytes, saving the received data
 * in <rxdata>.
 *
 * @param spi_device  the SPI device to use
 * @param txdata  buffer to transmit
 * @param txlen  number of bytes in txdata.
 * @param rxdata  receive buffer.
 * @param rxlen  number of bytes in rxdata.
 */
int spi_transaction(const struct spi_device_t *spi_device,
		    const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen);

/* Similar to spi_transaction(), but hands over to DMA for reading response.
 * Must call spi_transaction_flush() after this to make sure the response is
 * received.
 */
int spi_transaction_async(const struct spi_device_t *spi_device,
			  const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen);

/* Wait for async response received */
int spi_transaction_flush(const struct spi_device_t *spi_device);

#ifdef CONFIG_SPI
/**
 * Called when the NSS level changes, signalling the start or end of a SPI
 * transaction.
 *
 * @param signal	GPIO signal that changed
 */
void spi_event(enum gpio_signal signal);

#else
static inline void spi_event(enum gpio_signal signal)
{
}

#endif

#endif  /* __CROS_EC_SPI_H */
