/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MAX6958/MAX6959 7-Segment LED Display Driver
 */

#include "common.h"
#include "console.h"
#include "display_7seg.h"
#include "hooks.h"
#include "i2c.h"
#include "max695x.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static inline int max695x_i2c_write8(uint8_t offset, uint8_t data)
{
	return i2c_write8(I2C_PORT_PORT80, PORT80_I2C_ADDR,
			   offset, (int)data);
}

static inline int max695x_i2c_write(uint8_t offset, uint8_t *data, int len)
{
	/*
	 * The address pointer stored in the MAX695x increments after
	 * each data byte is written unless the address equals 01111111
	 */
	return i2c_write_block(I2C_PORT_PORT80, PORT80_I2C_ADDR,
			   offset, data, len);
}

int display_7seg_write(enum seven_seg_module_display module, uint16_t data)
{
	uint8_t buf[4];

	/*
	 * Convert the data into binary coded hexadecimal value i.e.
	 * in hexadecimal code-decode mode, the decoder prints 1 byte
	 * on two segments. It checks the lower nibble of the data in
	 * the digit register (D3–D0), disregarding bits D7–D4. Hence,
	 * preparing the hexadecimal buffer to be sent.
	 *
	 * Segment 3-2 : Module name
	 *	  0xEC : EC
	 *	  0x80 : PORT80
	 * Segment 1-0 : Data
	 * For console Command segment 3-0 : Data
	 */
	switch (module) {
	case SEVEN_SEG_CONSOLE_DISPLAY:
		/* Segment - 3 */
		buf[0] = (data >> 12) & 0x0F;
		/* Segment - 2 */
		buf[1] = (data >> 8) & 0x0F;
		break;
	case SEVEN_SEG_EC_DISPLAY:
		/* Segment - 3 */
		buf[0] = 0x0E;
		/* Segment - 2 */
		buf[1] = 0x0C;
		break;
	case SEVEN_SEG_PORT80_DISPLAY:
		/* Segment - 3 */
		buf[0] = 0x08;
		/* Segment - 2 */
		buf[1] = 0x00;
		break;
	default:
		CPRINTS("Unknown Module");
		return EC_ERROR_UNKNOWN;
	}
	/* Segment - 1 */
	buf[2] = (data >> 4) & 0x0F;
	/* Segment - 0 */
	buf[3] = data & 0x0F;

	return max695x_i2c_write(MAX695X_DIGIT0_ADDR, buf, ARRAY_SIZE(buf));
}

/**
 * Initialise MAX656x 7-segment display.
 */
static void max695x_init(void)
{
	uint8_t buf[4] = {
		[0] = MAX695X_DECODE_MODE_HEX_DECODE,
		[1] = MAX695X_INTENSITY_MEDIUM,
		[2] = MAX695X_SCAN_LIMIT_4,
		[3] = MAX695X_CONFIG_OPR_NORMAL
	};
	max695x_i2c_write(MAX695X_REG_DECODE_MODE, buf, ARRAY_SIZE(buf));
}
DECLARE_HOOK(HOOK_INIT, max695x_init, HOOK_PRIO_DEFAULT);

static void max695x_shutdown(void)
{
	max695x_i2c_write8(MAX695X_REG_CONFIG,
			   MAX695X_CONFIG_OPR_SHUTDOWN);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, max695x_shutdown, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_SEVEN_SEG_DISPLAY
static int console_command_max695x_write(int argc, char **argv)
{
	char *e;
	int val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Get value to be written to the seven segment display*/
	val = strtoi(argv[1], &e, 0);
	if (*e || val < 0 || val > UINT16_MAX)
		return EC_ERROR_PARAM1;

	return display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, val);
}
DECLARE_CONSOLE_COMMAND(seg, console_command_max695x_write,
			"<val>",
			"Write to 7 segment display in hex");
#endif
