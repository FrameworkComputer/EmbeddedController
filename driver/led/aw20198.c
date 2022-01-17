/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <string.h>

#include "aw20198.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "rgb_keyboard.h"
#include "stddef.h"
#include "timer.h"

#define CPRINTF(fmt, args...) cprintf(CC_RGBKBD, "AW20198: " fmt, ##args)
#define CPRINTS(fmt, args...) cprints(CC_RGBKBD, "AW20198: " fmt, ##args)

#define BUF_SIZE	(SIZE_OF_RGB * AW20198_GRID_SIZE)

static int aw20198_read(struct rgbkbd *ctx, uint8_t addr, uint8_t *value)
{
	return i2c_xfer(ctx->cfg->i2c, AW20198_I2C_ADDR_FLAG,
			&addr, sizeof(addr), value, sizeof(*value));
}

static int aw20198_write(struct rgbkbd *ctx, uint8_t addr, uint8_t value)
{
	uint8_t buf[2] = {
		[0] = addr,
		[1] = value,
	};

	return i2c_xfer(ctx->cfg->i2c, AW20198_I2C_ADDR_FLAG,
			buf, sizeof(buf), NULL, 0);
}

static int aw20198_set_page(struct rgbkbd *ctx, uint8_t page)
{
	return aw20198_write(ctx, AW20198_REG_PAGE, page);
}

static int aw20198_get_config(struct rgbkbd *ctx, uint8_t addr, uint8_t *value)
{
	int rv = aw20198_set_page(ctx, AW20198_PAGE_FUNC);
	if (rv) {
		return rv;
	}

	return aw20198_read(ctx, addr, value);
}

static int aw20198_set_config(struct rgbkbd *ctx, uint8_t addr, uint8_t value)
{
	int rv = aw20198_set_page(ctx, AW20198_PAGE_FUNC);
	if (rv) {
		return rv;
	}

	return aw20198_write(ctx, addr, value);
}

static int aw20198_reset(struct rgbkbd *ctx)
{
	return aw20198_set_config(ctx, AW20198_REG_RSTN, AW20198_RESET_MAGIC);
}

static int aw20198_enable(struct rgbkbd *ctx, bool enable)
{
	uint8_t cfg;
	int rv;

	rv = aw20198_get_config(ctx, AW20198_REG_GCR, &cfg);
	if (rv) {
		return rv;
	}

	return aw20198_write(ctx, AW20198_REG_GCR,
			      cfg | (enable ? BIT(0) : 0));
}

static int aw20198_set_color(struct rgbkbd *ctx, uint8_t offset,
			     struct rgb_s *color, uint8_t len)
{
	uint8_t buf[sizeof(offset) + BUF_SIZE];
	const int frame_len = len * SIZE_OF_RGB + sizeof(offset);
	const int frame_offset = offset * SIZE_OF_RGB;
	int i, rv;

	if (frame_offset + frame_len > sizeof(buf)) {
		return EC_ERROR_OVERFLOW;
	}

	rv = aw20198_set_page(ctx, AW20198_PAGE_PWM);
	if (rv) {
		return rv;
	}

	buf[0] = offset * SIZE_OF_RGB;
	for (i = 0; i < len; i++) {
		buf[i * SIZE_OF_RGB + 1] = color[i].r;
		buf[i * SIZE_OF_RGB + 2] = color[i].g;
		buf[i * SIZE_OF_RGB + 3] = color[i].b;
	}

	return i2c_xfer(ctx->cfg->i2c, AW20198_I2C_ADDR_FLAG,
			buf, frame_len, NULL, 0);
}

static int aw20198_set_scale(struct rgbkbd *ctx, uint8_t offset, uint8_t scale,
			     uint8_t len)
{
	uint8_t buf[sizeof(offset) + BUF_SIZE];
	const int frame_len = len * SIZE_OF_RGB + sizeof(offset);
	const int frame_offset = offset * SIZE_OF_RGB;
	int rv;

	if (frame_offset + frame_len > sizeof(buf)) {
		return EC_ERROR_OVERFLOW;
	}

	rv = aw20198_set_page(ctx, AW20198_PAGE_SCALE);
	if (rv) {
		return rv;
	}

	buf[0] = offset * SIZE_OF_RGB;
	memset(&buf[1], scale, len * SIZE_OF_RGB);

	return i2c_xfer(ctx->cfg->i2c, AW20198_I2C_ADDR_FLAG,
			buf, frame_len, NULL, 0);
}

static int aw20198_set_gcc(struct rgbkbd *ctx, uint8_t level)
{
	return aw20198_set_config(ctx, AW20198_REG_GCC, level);
}

static int aw20198_init(struct rgbkbd *ctx)
{
	uint8_t id;
	int rv;

	rv = aw20198_reset(ctx);
	msleep(3);
	rv |= aw20198_enable(ctx, true);
	if (rv) {
		CPRINTS("Failed to enable or reset (%d)", rv);
		return rv;
	}

	/* Read chip ID, assuming page is still 0. */
	rv = aw20198_read(ctx, AW20198_REG_RSTN, &id);
	if (rv) {
		return rv;
	}
	CPRINTS("ID=0x%02x", id);

	return rv;
}

const struct rgbkbd_drv aw20198_drv = {
	.reset = aw20198_reset,
	.init = aw20198_init,
	.enable = aw20198_enable,
	.set_color = aw20198_set_color,
	.set_scale = aw20198_set_scale,
	.set_gcc = aw20198_set_gcc,
};
