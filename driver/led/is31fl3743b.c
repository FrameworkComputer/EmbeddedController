/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "rgb_keyboard.h"
#include "spi.h"
#include "stddef.h"
#include "timer.h"

#include <string.h>

#define CPRINTF(fmt, args...) cprintf(CC_RGBKBD, "RGBKBD: " fmt, ##args)
#define CPRINTS(fmt, args...) cprints(CC_RGBKBD, "RGBKBD: " fmt, ##args)

#define SPI(id) (&(spi_devices[id]))

#define IS31FL3743B_ROW_SIZE 6
#define IS31FL3743B_COL_SIZE 11
#define IS31FL3743B_GRID_SIZE (IS31FL3743B_COL_SIZE * IS31FL3743B_ROW_SIZE)
#define IS31FL3743B_BUF_SIZE (SIZE_OF_RGB * IS31FL3743B_GRID_SIZE)

#define IS31FL3743B_CMD_ID 0b101
#define IS31FL3743B_PAGE_PWM 0
#define IS31FL3743B_PAGE_SCALE 1
#define IS31FL3743B_PAGE_FUNC 2

#define IS31FL3743B_REG_CONFIG 0x00
#define IS31FL3743B_REG_GCC 0x01
#define IS31FL3743B_REG_PD_PU 0x02
#define IS31FL3743B_REG_SPREAD_SPECTRUM 0x25
#define IS31FL3743B_REG_RSTN 0x2f

#define IS31FL3743B_CFG_SWS_1_11 0b0000
#define IS31FL3743B_CONFIG(sws, osde, ssd) \
	((sws) << 4 | BIT(3) | (osde) << 1 | (ssd) << 0)

struct is31fl3743b_cmd {
	uint8_t page : 4;
	uint8_t id : 3;
	uint8_t read : 1;
} __packed;

struct is31fl3743b_msg {
	struct is31fl3743b_cmd cmd;
	uint8_t addr;
	uint8_t payload[];
} __packed;

__maybe_unused static int is31fl3743b_read(struct rgbkbd *ctx, uint8_t addr,
					   uint8_t *value)
{
	uint8_t buf[8];
	struct is31fl3743b_msg *msg = (void *)buf;
	const int frame_len = sizeof(*msg);

	msg->cmd.read = 1;
	msg->cmd.id = IS31FL3743B_CMD_ID;
	msg->cmd.page = IS31FL3743B_PAGE_FUNC;
	msg->addr = addr;

	return spi_transaction(SPI(ctx->cfg->spi), buf, frame_len, value, 1);
}

static int is31fl3743b_write(struct rgbkbd *ctx, uint8_t addr, uint8_t value)
{
	uint8_t buf[8];
	struct is31fl3743b_msg *msg = (void *)buf;
	const int frame_len = sizeof(*msg) + 1;

	msg->cmd.read = 0;
	msg->cmd.id = IS31FL3743B_CMD_ID;
	msg->cmd.page = IS31FL3743B_PAGE_FUNC;
	msg->addr = addr;
	msg->payload[0] = value;

	return spi_transaction(SPI(ctx->cfg->spi), buf, frame_len, NULL, 0);
}

static int is31fl3743b_enable(struct rgbkbd *ctx, bool enable)
{
	uint8_t u8 =
		IS31FL3743B_CONFIG(IS31FL3743B_CFG_SWS_1_11, 0, enable ? 1 : 0);
	CPRINTS("Setting config register to 0x%x", u8);
	return is31fl3743b_write(ctx, IS31FL3743B_REG_CONFIG, u8);
}

static int is31fl3743b_set_color(struct rgbkbd *ctx, uint8_t offset,
				 struct rgb_s *color, uint8_t len)
{
	uint8_t buf[sizeof(struct is31fl3743b_msg) + IS31FL3743B_BUF_SIZE];
	struct is31fl3743b_msg *msg = (void *)buf;
	const int frame_len = len * SIZE_OF_RGB + sizeof(*msg);
	const int frame_offset = offset * SIZE_OF_RGB;
	int i;

	msg->cmd.read = 0;
	msg->cmd.id = IS31FL3743B_CMD_ID;
	msg->cmd.page = IS31FL3743B_PAGE_PWM;

	if (frame_offset + frame_len > sizeof(buf)) {
		return EC_ERROR_OVERFLOW;
	}

	msg->addr = frame_offset + 1; /* Register addr base is 1. */
	for (i = 0; i < len; i++) {
		msg->payload[i * SIZE_OF_RGB + 0] = color[i].r;
		msg->payload[i * SIZE_OF_RGB + 1] = color[i].g;
		msg->payload[i * SIZE_OF_RGB + 2] = color[i].b;
	}

	return spi_transaction(SPI(ctx->cfg->spi), buf, frame_len, NULL, 0);
}

static int is31fl3743b_set_scale(struct rgbkbd *ctx, uint8_t offset,
				 struct rgb_s scale, uint8_t len)
{
	uint8_t buf[sizeof(struct is31fl3743b_msg) + IS31FL3743B_BUF_SIZE];
	struct is31fl3743b_msg *msg = (void *)buf;
	const int frame_len = len * SIZE_OF_RGB + sizeof(*msg);
	const int frame_offset = offset * SIZE_OF_RGB;
	int i;

	msg->cmd.read = 0;
	msg->cmd.id = IS31FL3743B_CMD_ID;
	msg->cmd.page = IS31FL3743B_PAGE_SCALE;

	if (frame_offset + frame_len > sizeof(buf)) {
		return EC_ERROR_OVERFLOW;
	}

	msg->addr = frame_offset + 1; /* Address base is 1. */
	for (i = 0; i < len; i++) {
		msg->payload[i * SIZE_OF_RGB + 0] = scale.r;
		msg->payload[i * SIZE_OF_RGB + 1] = scale.g;
		msg->payload[i * SIZE_OF_RGB + 2] = scale.b;
	}

	return spi_transaction(SPI(ctx->cfg->spi), buf, frame_len, NULL, 0);
}

static int is31fl3743b_set_gcc(struct rgbkbd *ctx, uint8_t level)
{
	uint8_t buf[8];
	struct is31fl3743b_msg *msg = (void *)buf;
	const int frame_len = sizeof(*msg) + 1;

	msg->cmd.read = 0;
	msg->cmd.id = IS31FL3743B_CMD_ID;
	msg->cmd.page = IS31FL3743B_PAGE_FUNC;
	msg->addr = IS31FL3743B_REG_GCC;
	msg->payload[0] = level;

	return spi_transaction(SPI(ctx->cfg->spi), buf, frame_len, NULL, 0);
}

static int is31fl3743b_init(struct rgbkbd *ctx)
{
	int rv;

	/* Reset registers to the default values. */
	rv = is31fl3743b_write(ctx, IS31FL3743B_REG_RSTN, 0xae);
	if (rv)
		return rv;
	crec_msleep(3);

	return EC_SUCCESS;
}

const struct rgbkbd_drv is31fl3743b_drv = {
	.init = is31fl3743b_init,
	.enable = is31fl3743b_enable,
	.set_color = is31fl3743b_set_color,
	.set_scale = is31fl3743b_set_scale,
	.set_gcc = is31fl3743b_set_gcc,
};
