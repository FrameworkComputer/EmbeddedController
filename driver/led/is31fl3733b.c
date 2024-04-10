/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "rgb_keyboard.h"
#include "stddef.h"
#include "timer.h"

#include <string.h>

#define CPRINTF(fmt, args...) cprintf(CC_RGBKBD, "RGBKBD: " fmt, ##args)
#define CPRINTS(fmt, args...) cprints(CC_RGBKBD, "RGBKBD: " fmt, ##args)

/* This depends on ADDR1 and ADDR2. (GND, GND) = 0x50. */
#define IS31FL3733B_ADDR_FLAGS 0x50

#define IS31FL3733B_ROW_SIZE 16
#define IS31FL3733B_COL_SIZE 4
#define IS31FL3733B_GRID_SIZE (IS31FL3733B_COL_SIZE * IS31FL3733B_ROW_SIZE)
#define IS31FL3733B_BUF_SIZE (SIZE_OF_RGB * IS31FL3733B_GRID_SIZE)

/* IS31FL3733B registers */
#define IS31FL3733B_REG_COMMAND 0xFD
#define IS31FL3733B_REG_COMMAND_WRITE_LOCK 0xFE
#define IS31FL3733B_REG_INT_MASK 0xF0
#define IS31FL3733B_REG_INT_STATUS 0xF1

#define IS31FL3733B_PAGE_CTRL 0x00
#define IS31FL3733B_PAGE_PWM 0x01
#define IS31FL3733B_PAGE_AUTO 0x02
#define IS31FL3733B_PAGE_FUNC 0x03

/* FEh Command Register Write Lock */
#define IS31FL3733B_WRITE_DISABLE 0x00
#define IS31FL3733B_WRITE_ENABLE 0xC5

#define IS31FL3733B_INT_MASK_IAC BIT(3)
#define IS31FL3733B_INT_MASK_IAB BIT(2)
#define IS31FL3733B_INT_MASK_IS BIT(1)
#define IS31FL3733B_INT_MASK_IO BIT(0)
#define IS31FL3733B_INT_STATUS_ABM3 BIT(4)
#define IS31FL3733B_INT_STATUS_ABM2 BIT(3)
#define IS31FL3733B_INT_STATUS_ABM1 BIT(2)
#define IS31FL3733B_INT_STATUS_SB BIT(1)
#define IS31FL3733B_INT_STATUS_OB BIT(0)

#define IS31FL3733B_FUNC_CFG 0x00
#define IS31FL3733B_FUNC_GCC 0x01
#define IS31FL3733B_FUNC_ABM1_1 0x02
#define IS31FL3733B_FUNC_ABM1_2 0x03
#define IS31FL3733B_FUNC_ABM1_3 0x04
#define IS31FL3733B_FUNC_ABM1_4 0x05
#define IS31FL3733B_FUNC_ABM2_1 0x06
#define IS31FL3733B_FUNC_ABM2_2 0x07
#define IS31FL3733B_FUNC_ABM2_3 0x08
#define IS31FL3733B_FUNC_ABM2_4 0x09
#define IS31FL3733B_FUNC_ABM3_1 0x0a
#define IS31FL3733B_FUNC_ABM3_2 0x0b
#define IS31FL3733B_FUNC_ABM3_3 0x0c
#define IS31FL3733B_FUNC_ABM3_4 0x0d
#define IS31FL3733B_FUNC_TUR 0x0e
#define IS31FL3733B_FUNC_SW_PU 0x0f
#define IS31FL3733B_FUNC_CS_PD 0x10
#define IS31FL3733B_FUNC_RST 0x11

static int is31fl3733b_read(struct rgbkbd *ctx, uint8_t addr, uint8_t *value)
{
	return i2c_xfer(ctx->cfg->i2c, IS31FL3733B_ADDR_FLAGS, &addr,
			sizeof(addr), value, sizeof(*value));
}

static int is31fl3733b_write(struct rgbkbd *ctx, uint8_t addr, uint8_t value)
{
	uint8_t buf[2] = {
		[0] = addr,
		[1] = value,
	};

	return i2c_xfer(ctx->cfg->i2c, IS31FL3733B_ADDR_FLAGS, buf, sizeof(buf),
			NULL, 0);
}

static int is31fl3733b_set_page(struct rgbkbd *ctx, uint8_t page)
{
	int rv;

	/* unlock page select once */
	rv = is31fl3733b_write(ctx, IS31FL3733B_REG_COMMAND_WRITE_LOCK,
			       IS31FL3733B_WRITE_ENABLE);
	if (rv) {
		return rv;
	}

	return is31fl3733b_write(ctx, IS31FL3733B_REG_COMMAND, page);
}

static int is31fl3733b_get_config(struct rgbkbd *ctx, uint8_t addr,
				  uint8_t *value)
{
	int rv;

	rv = is31fl3733b_set_page(ctx, IS31FL3733B_PAGE_FUNC);
	if (rv) {
		return rv;
	}

	return is31fl3733b_read(ctx, addr, value);
}

static int is31fl3733b_set_config(struct rgbkbd *ctx, uint8_t addr,
				  uint8_t value)
{
	int rv;

	rv = is31fl3733b_set_page(ctx, IS31FL3733B_PAGE_FUNC);
	if (rv) {
		return rv;
	}

	return is31fl3733b_write(ctx, addr, value);
}

static int is31fl3733b_reset(struct rgbkbd *ctx)
{
	uint8_t value;

	return is31fl3733b_get_config(ctx, IS31FL3733B_FUNC_RST, &value);
}

static int is31fl3733b_enable(struct rgbkbd *ctx, bool enable)
{
	uint8_t u8;
	int rv;

	rv = is31fl3733b_set_page(ctx, IS31FL3733B_PAGE_FUNC);
	if (rv) {
		return rv;
	}

	u8 = 0;

	WRITE_BIT(u8, 4, 1);
	WRITE_BIT(u8, 0, enable);

	return is31fl3733b_write(ctx, IS31FL3733B_FUNC_CFG, u8);
}

static int is31fl3733b_set_color(struct rgbkbd *ctx, uint8_t offset,
				 struct rgb_s *color, uint8_t len)
{
	int led_addr, led_addr_row, led_addr_col;
	int i, rv;

	rv = is31fl3733b_set_page(ctx, IS31FL3733B_PAGE_PWM);
	if (rv) {
		return rv;
	}

	for (i = 0; i < len; i++) {
		led_addr_row = (offset + i) % ctx->cfg->row_len;
		led_addr_col = (offset + i) / ctx->cfg->row_len;
		led_addr = led_addr_row * 0x30 + led_addr_col;

		rv = is31fl3733b_write(ctx, led_addr + 0x00, color[i].r);
		rv |= is31fl3733b_write(ctx, led_addr + 0x10, color[i].g);
		rv |= is31fl3733b_write(ctx, led_addr + 0x20, color[i].b);

		if (rv) {
			return rv;
		}
	}

	return EC_SUCCESS;
}

static int is31fl3733b_set_scale(struct rgbkbd *ctx, uint8_t offset,
				 struct rgb_s scale, uint8_t len)
{
	/* is31fl3733b not support scale function */
	return EC_SUCCESS;
}

static int is31fl3733b_set_gcc(struct rgbkbd *ctx, uint8_t level)
{
	return is31fl3733b_set_config(ctx, IS31FL3733B_FUNC_GCC, level);
}

static int is31fl3733b_init(struct rgbkbd *ctx)
{
	int i, rv;

	rv = is31fl3733b_reset(ctx);
	crec_msleep(3);

	/* enable all led */
	rv = is31fl3733b_set_page(ctx, IS31FL3733B_PAGE_CTRL);
	if (rv) {
		return rv;
	}

	for (i = 0; i < 0x18; i++) {
		rv = is31fl3733b_write(ctx, i, 0xff);
		if (rv)
			CPRINTS("LED 0x%02x init fail (rv=%d)", i, rv);
	}

	if (IS_ENABLED(CONFIG_RGB_KEYBOARD_DEBUG)) {
		uint8_t val;
		int ret;

		ret = is31fl3733b_get_config(ctx, IS31FL3733B_FUNC_SW_PU, &val);
		CPRINTS("SW_PU: val=0x%02x (rv=%d)", val, ret);

		ret = is31fl3733b_get_config(ctx, IS31FL3733B_FUNC_CS_PD, &val);
		CPRINTS("CS_PD: val=0x%02x (rv=%d)", val, ret);
	}

	return rv;
}

const struct rgbkbd_drv is31fl3733b_drv = {
	.reset = is31fl3733b_reset,
	.init = is31fl3733b_init,
	.enable = is31fl3733b_enable,
	.set_color = is31fl3733b_set_color,
	.set_scale = is31fl3733b_set_scale,
	.set_gcc = is31fl3733b_set_gcc,
};
