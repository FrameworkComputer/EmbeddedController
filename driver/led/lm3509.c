/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3509 LED driver.
 */

#include "compile_time_macros.h"
#include "i2c.h"
#include "keyboard_backlight.h"
#include "lm3509.h"

static inline int lm3509_write(uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_KBLIGHT, LM3509_I2C_ADDR_FLAGS,
			  reg, val);
}

static inline int lm3509_read(uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_KBLIGHT, LM3509_I2C_ADDR_FLAGS,
			 reg, val);
}

/* Brightness level (0.0 to 100.0%) to brightness register conversion table */
static const uint16_t lm3509_brightness[32] = {
	  0,   1,   6,  10,  11,  13,  16,  20,
	 24,  28,  31,  37,  43,  52,  62,  75,
	 87, 100, 125, 150, 168, 187, 225, 262,
	312, 375, 437, 525, 612, 700, 875, 1000
};

static int brightness_to_bmain(int percent)
{
	int i;
	int b = percent * 10;

	for (i = 1; i < sizeof(lm3509_brightness); i++) {
		int low = lm3509_brightness[i - 1];
		int high = lm3509_brightness[i];
		if (high < b)
			continue;
		/* rounding to the nearest */
		return (b - low < high - b) ? i - 1 : i;
	}
	/* Brightness is out of range. Return the highest value. */
	return i - 1;
}

static int lm3509_power(int enable)
{
	/* Enable both MAIN and SUB in unison mode.
	 * Don't alter brightness here. It's not driver's business. */
	return lm3509_write(LM3509_REG_GP, enable ? 0x7 : 0);
}

static int lm3509_set_brightness(int percent)
{
	/* We don't need to read/mask/write BMAIN because bit6 and 7 are non
	 * functional read only bits.
	 */
	return lm3509_write(LM3509_REG_BMAIN, brightness_to_bmain(percent));
}

static int lm3509_get_brightness(void)
{
	int rv, val;
	rv = lm3509_read(LM3509_REG_BMAIN, &val);
	if (rv)
		return -1;
	val &= LM3509_BMAIN_MASK;
	return lm3509_brightness[val] / 10;
}

static int lm3509_init(void)
{
	return EC_SUCCESS;
}

const struct kblight_drv kblight_lm3509 = {
	.init = lm3509_init,
	.set = lm3509_set_brightness,
	.get = lm3509_get_brightness,
	.enable = lm3509_power,
};
