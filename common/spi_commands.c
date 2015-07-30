/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI transfer command for debugging SPI devices.
 */

#include "common.h"
#include "console.h"
#include "spi.h"
#include "util.h"

static int command_spixfer(int argc, char **argv)
{
	int dev_id;
	uint8_t offset;
	int v = 0;
	uint8_t data[32];
	char *e;
	int rv = 0;

	if (argc != 5)
		return EC_ERROR_PARAM_COUNT;

	dev_id = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	offset = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	v = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	if (strcasecmp(argv[1], "rlen") == 0) {
		uint8_t cmd = 0x80 | offset;

		/* Arbitrary length read; param4 = len */
		if (v < 0 || v > sizeof(data))
			return EC_ERROR_PARAM4;

		rv = spi_transaction(&spi_devices[dev_id], &cmd, 1, data, v);

		if (!rv)
			ccprintf("Data: %.*h\n", v, data);

	} else if (strcasecmp(argv[1], "w") == 0) {
		/* 8-bit write */
		uint8_t cmd[2] = { offset, v };

		rv = spi_transaction(&spi_devices[dev_id], cmd, 2, NULL, 0);
	} else {
		return EC_ERROR_PARAM1;
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(spixfer, command_spixfer,
			"rlen/w id offset [value | len]",
			"Read write spi. id is spi_devices array index",
			NULL);

