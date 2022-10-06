/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <string.h>

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "rgb_keyboard.h"
#include "stddef.h"
#include "tlc59116f.h"

#define CPRINTF(fmt, args...) cprintf(CC_RGBKBD, "TLC59116F: " fmt, ##args)
#define CPRINTS(fmt, args...) cprints(CC_RGBKBD, "TLC59116F: " fmt, ##args)

#define TLC59116F_BUF_SIZE (SIZE_OF_RGB * TLC59116F_GRID_SIZE)
#define TLC59116_MODE_BIT_SLEEP 4

static int tlc59116f_read(struct rgbkbd *ctx, uint8_t addr, uint8_t *value)
{
	return i2c_xfer(ctx->cfg->i2c, TLC59116F_I2C_ADDR_FLAG, &addr,
			sizeof(addr), value, sizeof(*value));
}

static int tlc59116f_write(struct rgbkbd *ctx, uint8_t addr, uint8_t value)
{
	uint8_t buf[2] = {
		[0] = addr,
		[1] = value,
	};

	return i2c_xfer(ctx->cfg->i2c, TLC59116F_I2C_ADDR_FLAG, buf,
			sizeof(buf), NULL, 0);
}

static int tlc59116f_reset(struct rgbkbd *ctx)
{
	return i2c_write8(ctx->cfg->i2c, TLC59116F_RESET, 0xA5, 0x5A);
}

static int tlc59116f_init(struct rgbkbd *ctx)
{
	int i, rv;

	for (i = TLC59116F_LEDOUT0; i <= TLC59116F_LEDOUT3; i++) {
		rv = tlc59116f_write(ctx, i, TLC59116_LEDOUT_PWM);
		if (rv) {
			return rv;
		}
	}

	rv = tlc59116f_write(ctx, TLC59116F_MODE1, 0x01);
	if (rv) {
		CPRINTS("Failed to set TLC59116F normal mode");
		return rv;
	}

	return EC_SUCCESS;
}

static int tlc59116f_enable(struct rgbkbd *ctx, bool enable)
{
	uint8_t cfg;
	int rv;

	rv = tlc59116f_read(ctx, TLC59116F_MODE1, &cfg);
	if (rv) {
		CPRINTS("Failed to enable TLC59116F");
		return rv;
	}

	WRITE_BIT(cfg, TLC59116_MODE_BIT_SLEEP, !enable);
	return tlc59116f_write(ctx, TLC59116F_MODE1, cfg);
}

static int tlc59116f_set_color(struct rgbkbd *ctx, uint8_t offset,
			       struct rgb_s *color, uint8_t len)
{
	uint8_t buf[sizeof(offset) + TLC59116F_BUF_SIZE];
	const int frame_len = len * SIZE_OF_RGB + sizeof(offset);
	const int frame_offset = offset * SIZE_OF_RGB;
	int i;

	if (frame_offset + frame_len > sizeof(buf)) {
		return EC_ERROR_OVERFLOW;
	}

	buf[0] = TLC59116_AI_BRIGHTNESS_ONLY | (frame_offset + TLC59116F_PWM0);
	for (i = 0; i < len; i++) {
		buf[i * SIZE_OF_RGB + 1] = color[i].r;
		buf[i * SIZE_OF_RGB + 2] = color[i].g;
		buf[i * SIZE_OF_RGB + 3] = color[i].b;
	}

	return i2c_xfer(ctx->cfg->i2c, TLC59116F_I2C_ADDR_FLAG, buf, frame_len,
			NULL, 0);
}

static int tlc59116f_set_scale(struct rgbkbd *ctx, uint8_t offset,
			       struct rgb_s scale, uint8_t len)
{
	/* tlc59116f not support scale function */
	return EC_SUCCESS;
}

static int tlc59116f_set_gcc(struct rgbkbd *ctx, uint8_t level)
{
	int j, rv;

	for (j = TLC59116F_LEDOUT0; j <= TLC59116F_LEDOUT3; j++) {
		rv = tlc59116f_write(ctx, j, TLC59116_LEDOUT_GROUP);
		if (rv) {
			return rv;
		}
	}

	return tlc59116f_write(ctx, TLC59116F_GRPPWM, level);
}

const struct rgbkbd_drv tlc59116f_drv = {
	.reset = tlc59116f_reset,
	.init = tlc59116f_init,
	.enable = tlc59116f_enable,
	.set_color = tlc59116f_set_color,
	.set_scale = tlc59116f_set_scale,
	.set_gcc = tlc59116f_set_gcc,
};
