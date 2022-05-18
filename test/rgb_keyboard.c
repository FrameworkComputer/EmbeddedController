/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for RGB keyboard.
 */
#include <stdio.h>

#include "common.h"
#include "console.h"
#include "keyboard_backlight.h"
#include "rgb_keyboard.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define RGB_GRID0_COL 11
#define RGB_GRID0_ROW 6
#define RGB_GRID1_COL 11
#define RGB_GRID1_ROW 6
#define SPI_RGB0_DEVICE_ID 0
#define SPI_RGB1_DEVICE_ID 1

static struct rgb_s grid0[RGB_GRID0_COL * RGB_GRID0_ROW];
static struct rgb_s grid1[RGB_GRID1_COL * RGB_GRID1_ROW];

const struct rgbkbd_drv test_drv;

struct rgbkbd rgbkbds[] = {
	[0] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &test_drv,
			.spi = SPI_RGB0_DEVICE_ID,
			.col_len = RGB_GRID0_COL,
			.row_len = RGB_GRID0_ROW,
		},
		.buf = grid0,
	},
	[1] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &test_drv,
			.spi = SPI_RGB1_DEVICE_ID,
			.col_len = RGB_GRID1_COL,
			.row_len = RGB_GRID1_ROW,
		},
		.buf = grid1,
	},
};
const uint8_t rgbkbd_count = ARRAY_SIZE(rgbkbds);
const uint8_t rgbkbd_hsize = RGB_GRID0_COL + RGB_GRID1_COL;
const uint8_t rgbkbd_vsize = RGB_GRID0_ROW;

const uint8_t rgbkbd_map[] = {
	RGBKBD_DELM,
	RGBKBD_COORD(1, 2), RGBKBD_DELM,
	RGBKBD_COORD(3, 4), RGBKBD_COORD(5, 6), RGBKBD_DELM,
	RGBKBD_DELM,
	RGBKBD_DELM,
};
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

extern const uint8_t rgbkbd_table[EC_RGBKBD_MAX_KEY_COUNT];

static struct rgbkbd_mock {
	uint32_t count_drv_reset;
	uint32_t count_drv_init;
	uint32_t count_drv_enable;
	uint32_t count_drv_set_color;
	uint32_t count_drv_set_scale;
	uint32_t count_drv_set_gcc;
	uint32_t gcc_level;
} mock_state;

__override void board_kblight_init(void) {}

__override void board_kblight_shutdown(void) {}

void before_test(void)
{
	memset(&mock_state, 0, sizeof(mock_state));
}

static int test_drv_reset(struct rgbkbd *ctx)
{
	mock_state.count_drv_reset++;
	return EC_SUCCESS;
}

static int test_drv_init(struct rgbkbd *ctx)
{
	mock_state.count_drv_init++;
	return EC_SUCCESS;
}

static int test_drv_enable(struct rgbkbd *ctx, bool enable)
{
	mock_state.count_drv_enable++;
	return EC_SUCCESS;
}

static int test_drv_set_color(struct rgbkbd *ctx, uint8_t offset,
			      struct rgb_s *color, uint8_t len)
{
	mock_state.count_drv_set_color++;
	return EC_SUCCESS;
}


static int test_drv_set_scale(struct rgbkbd *ctx, uint8_t offset,
			      uint8_t scale, uint8_t len)
{
	mock_state.count_drv_set_scale++;
	return EC_SUCCESS;
}

static int test_drv_set_gcc(struct rgbkbd *ctx, uint8_t level)
{
	mock_state.count_drv_set_gcc++;
	mock_state.gcc_level = level;
	return EC_SUCCESS;
}

void rgbkbd_init_lookup_table(void);

static int test_rgbkbd_map(void)
{
	union rgbkbd_coord_u8 led;

	rgbkbd_init_lookup_table();

	led.u8 = rgbkbd_map[rgbkbd_table[0]];
	zassert_equal(RGBKBD_COORD(led.coord.x, led.coord.y),
		      RGBKBD_DELM, "key[0] -> None");

	led.u8 = rgbkbd_map[rgbkbd_table[1]];
	zassert_equal(RGBKBD_COORD(led.coord.x, led.coord.y),
		      RGBKBD_COORD(1, 2), "key[1] -> LED(1,2)");

	led.u8 = rgbkbd_map[rgbkbd_table[2]];
	zassert_equal(RGBKBD_COORD(led.coord.x, led.coord.y),
		      RGBKBD_COORD(3, 4), "key[2] -> LED(3,4)");
	led.u8 = rgbkbd_map[rgbkbd_table[2] + 1];
	zassert_equal(RGBKBD_COORD(led.coord.x, led.coord.y),
		      RGBKBD_COORD(5, 6), "key[2] -> LED(5,6)");

	led.u8 = rgbkbd_map[rgbkbd_table[3]];
	zassert_equal(RGBKBD_COORD(led.coord.x, led.coord.y),
		      RGBKBD_DELM, "key[3] -> None");

	led.u8 = rgbkbd_map[rgbkbd_table[4]];
	zassert_equal(RGBKBD_COORD(led.coord.x, led.coord.y),
		      RGBKBD_DELM, "key[4] -> None");

	return EC_SUCCESS;
}

const struct rgbkbd_drv test_drv = {
	.reset = test_drv_reset,
	.init = test_drv_init,
	.enable = test_drv_enable,
	.set_color = test_drv_set_color,
	.set_scale = test_drv_set_scale,
	.set_gcc = test_drv_set_gcc,
};

static int test_rgbkbd_startup(void)
{
	struct rgbkbd *ctx;
	struct rgb_s color;
	int g, x, y, c, r;

	/* Let RGBKBD task run. */
	task_wait_event(-1);

	/* Check 'DOT' demo. */
	for (x = 0; x < rgbkbd_hsize; x++) {
		g = x / rgbkbds[0].cfg->col_len;
		c = x % rgbkbds[0].cfg->col_len;
		ctx = &rgbkbds[g];
		for (y = 0; y < ctx->cfg->row_len; y++) {
			r = y;
			color = ctx->buf[ctx->cfg->row_len * c + r];
			zassert_equal(color.r, 0, "R = 0");
			zassert_equal(color.g, 0, "G = 0");
			zassert_equal(color.b, 0, "B = 0");

			r++;
			if (r >= ctx->cfg->row_len) {
				r = 0;
				c++;
				if (c >= rgbkbds[0].cfg->col_len) {
					task_wait_event(-1);
					break;
				}
			}
			color = ctx->buf[ctx->cfg->row_len * c + r];
			zassert_equal(color.r, 0x80, "R = 0x80");
			zassert_equal(color.g, 0, "G = 0");
			zassert_equal(color.b, 0, "B = 0");

			task_wait_event(-1);
		}
	}

	return EC_SUCCESS;
}

int cc_rgb(int argc, char **argv);
extern enum rgbkbd_demo demo;

static int test_rgbkbd_console_command(void)
{
	struct rgbkbd *ctx;
	int argc;
	char buf[8];
	int i, x, y, r, c;
	uint8_t offset;
	char *argv_demo[] = {"rgbk", "demo", "0"};
	char *argv_gcc[] = {"rgbk", "100"};
	char *argv_color[] = {"rgbk", buf, "1", "2", "3"};
	char *argv_all[] = {"rgbk", "all", "1", "2", "3"};

	/* Test 'rgbk demo 0'. */
	before_test();
	argc = ARRAY_SIZE(argv_demo);
	zassert_equal(demo, 2, "demo == 2");
	zassert_equal(cc_rgb(argc, argv_demo), EC_SUCCESS, "rgbk demo 0");
	zassert_equal(demo, 0, "demo == 0");

	/* Test 'rgbk 100'. */
	before_test();
	argc = ARRAY_SIZE(argv_gcc);
	zassert_equal(cc_rgb(argc, argv_gcc), EC_SUCCESS, "rgbk 100");
	zassert_equal(mock_state.count_drv_set_gcc, rgbkbd_count,
		      "set_gcc() called");
	zassert_equal(mock_state.gcc_level, 100, "gcc == 100");

	/* Test 'rgbk 1,1 1 2 3'. */
	before_test();
	ctx = &rgbkbds[0];
	x = 1;
	y = 1;
	offset = rgbkbd_vsize * x + y;
	sprintf(buf, "%d,%d", x, y);
	argc = ARRAY_SIZE(argv_color);
	zassert_equal(cc_rgb(argc, argv_color), EC_SUCCESS,
		      "rgbk %s 1 2 3", buf);
	zassert_equal(ctx->buf[offset].r, 1, "R = 1");
	zassert_equal(ctx->buf[offset].g, 2, "G = 2");
	zassert_equal(ctx->buf[offset].b, 3, "B = 3");

	/* Test 'rgbk 1,-1 1 2 3'. */
	before_test();
	ctx = &rgbkbds[0];
	x = 1;
	y = -1;
	sprintf(buf, "%d,%d", x, y);
	argc = ARRAY_SIZE(argv_color);
	zassert_equal(cc_rgb(argc, argv_color), EC_SUCCESS,
		      "rgbk %s 1 2 3", buf);
	for (r = 0; r < rgbkbd_vsize; r++) {
		offset = rgbkbd_vsize * x + r;
		zassert_equal(ctx->buf[offset].r, 1, "R = 1");
		zassert_equal(ctx->buf[offset].g, 2, "G = 2");
		zassert_equal(ctx->buf[offset].b, 3, "B = 3");
	}

	/* Test 'rgbk -1,1 1 2 3'. */
	before_test();
	x = -1;
	y = 1;
	sprintf(buf, "%d,%d", x, y);
	argc = ARRAY_SIZE(argv_color);
	zassert_equal(cc_rgb(argc, argv_color), EC_SUCCESS,
		      "rgbk %s 1 2 3", buf);
	for (c = 0; c < rgbkbd_hsize; c++) {
		ctx = &rgbkbds[c / rgbkbds[0].cfg->col_len];
		offset = rgbkbd_vsize * (c % ctx->cfg->col_len) + y;
		zassert_equal(ctx->buf[offset].r, 1, "R = 1");
		zassert_equal(ctx->buf[offset].g, 2, "G = 2");
		zassert_equal(ctx->buf[offset].b, 3, "B = 3");
	}

	/* Test 'rgbk all 1 2 3'. */
	before_test();
	argc = ARRAY_SIZE(argv_all);
	zassert_equal(cc_rgb(argc, argv_all), EC_SUCCESS, "rgbk all 1 2 3");
	for (i = 0; i < rgbkbd_count; i++) {
		ctx = &rgbkbds[i];
		for (c = 0; c < ctx->cfg->col_len; c++) {
			for (r = 0; r < ctx->cfg->row_len; r++) {
				offset = rgbkbd_vsize * c + r;
				zassert_equal(ctx->buf[offset].r, 1, "R = 1");
				zassert_equal(ctx->buf[offset].g, 2, "G = 2");
				zassert_equal(ctx->buf[offset].b, 3, "B = 3");
			}
		}
	}

	return EC_SUCCESS;
}

struct rgb_s rotate_color(struct rgb_s color, int step);
uint8_t get_grid_size(const struct rgbkbd *ctx);

static int test_rgbkbd_rotate_color(void)
{
	struct rgb_s color = {};
	const int step = 32;
	int r, g, b;

	for (b = 0; b < 0x100 / step; b++) {
		zassert_equal(color.r, 0, "R = 0");
		zassert_equal(color.g, 0, "G = 0");
		zassert_equal(color.b, b * step, "B += 32");
		for (g = 0; g < 0x100 / step; g++) {
			zassert_equal(color.r, 0, "R = 0");
			zassert_equal(color.g, g * step, "G += 32");
			zassert_equal(color.b, b * step, "B = B");
			for (r = 0; r < 0x100 / step; r++) {
				zassert_equal(color.r, r * step, "R += 32");
				zassert_equal(color.g, g * step, "G = G");
				zassert_equal(color.b, b * step, "B = B");
				color = rotate_color(color, step);
			}
		}
	}
	zassert_equal(color.r, 0, "R = 0");
	zassert_equal(color.g, 0, "G = 0");
	zassert_equal(color.b, 0, "B = 0");

	return EC_SUCCESS;
}

static int test_rgbkbd_demo_flow(void)
{
	struct rgb_s copy[ARRAY_SIZE(rgbkbds)][RGB_GRID0_COL * RGB_GRID0_ROW];
	char *argv_demo[] = {"rgbk", "demo", "1"};
	struct rgb_s *p;
	int argc;
	struct rgbkbd *ctx;
	struct rgb_s color = {};
	const int step = 32;
	uint8_t len;
	int i, j, g;

	argc = ARRAY_SIZE(argv_demo);
	zassert_equal(cc_rgb(argc, argv_demo), EC_SUCCESS, "rgbk demo flow");

	for (j = 0; j < 0x100 / step; j++) {
		/* Take a snapshot. */
		memcpy(&copy[0], rgbkbds[0].buf, sizeof(copy[0]));
		memcpy(&copy[1], rgbkbds[1].buf, sizeof(copy[1]));

		/* Let demo run one iteration. */
		task_wait_event(-1);

		/* Compare with the snapshot. */
		for (g = rgbkbd_count - 1; g >= 0; g--) {
			ctx = &rgbkbds[g];
			p = copy[g];
			len = get_grid_size(ctx);
			for (i = len - 1; i > 0; i--) {
				zassert_equal(ctx->buf[i].r, p[i - 1].r,
					      "i <- i-1");
			}
			if (g > 0) {
				len = get_grid_size(&rgbkbds[g - 1]);
				p = copy[g - 1];
				zassert_equal(ctx->buf[0].r, p[len - 1].r,
					      "grid1[0] <- grid0[last]");
			}
		}

		/* After one run, a new color will be injected to (0,0). */
		color = rotate_color(color, step);
		zassert_equal(ctx->buf[0].r, color.r, "(0,0) <- new color");
	}

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_rgbkbd_startup);
	RUN_TEST(test_rgbkbd_console_command);
	RUN_TEST(test_rgbkbd_rotate_color);
	RUN_TEST(test_rgbkbd_demo_flow);
	RUN_TEST(test_rgbkbd_map);
	test_print_result();
}
