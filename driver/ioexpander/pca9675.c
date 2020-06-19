/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA9675PW I/O Port expander driver source
 */

/* TODO (b/169814014): Implement code to fit "struct ioexpander_drv" */

#include "pca9675.h"

static uint16_t cache_out_pins[CONFIG_IO_EXPANDER_PORT_COUNT];

static int pca9675_read(int ioex, uint16_t *data)
{
	return i2c_xfer(pca9675_iox[ioex].i2c_host_port,
			pca9675_iox[ioex].i2c_addr_flags,
			NULL, 0, (uint8_t *)data, 2);
}

static int pca9675_write(int ioex, uint16_t data)
{
	/*
	 * PCA9675 is Quasi-bidirectional I/O architecture hence
	 * append the direction (1 = input, 0 = output) to prevent
	 * overwriting I/O pins inadvertently.
	 */
	data |= pca9675_iox[ioex].io_direction;

	return i2c_xfer(pca9675_iox[ioex].i2c_host_port,
			pca9675_iox[ioex].i2c_addr_flags,
			(uint8_t *)&data, 2, NULL, 0);
}

static int pca9675_reset(int ioex)
{
	uint8_t reset = PCA9675_RESET_SEQ_DATA;

	return i2c_xfer(pca9675_iox[ioex].i2c_host_port,
			0, &reset, 1, NULL, 0);
}

int pca9675_get_pin(int ioex, uint16_t pin, bool *level)
{
	int rv = EC_SUCCESS;
	uint16_t data_read;

	/* Read from IO-expander only if the pin is input */
	if (pca9675_iox[ioex].io_direction & pin) {
		rv = pca9675_read(ioex, &data_read);
		if (!rv)
			*level = !!(data_read & pin);
	} else {
		*level = !!(cache_out_pins[ioex] & pin);
	}

	return rv;
}

int pca9675_update_pins(int ioex, uint16_t setpins, uint16_t clearpins)
{
	/* Update the output pins */
	cache_out_pins[ioex] |= setpins;
	cache_out_pins[ioex] &= ~clearpins;

	return pca9675_write(ioex, cache_out_pins[ioex]);
}

int pca9675_init(int ioex)
{
	int rv;

	/* Set pca9675 to Power-on reset */
	rv = pca9675_reset(ioex);
	if (rv)
		return rv;

	/* Initialize the I/O direction */
	return pca9675_write(ioex, 0);
}
