/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdbool.h>

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "registers.h"
#include "rgb_keyboard.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_RGBKBD, outstr)
#define CPRINTF(fmt, args...) cprintf(CC_RGBKBD, "RGBKBD: " fmt, ##args)
#define CPRINTS(fmt, args...) cprints(CC_RGBKBD, "RGBKBD: " fmt, ##args)

test_export_static enum rgbkbd_demo demo =
#if   defined(CONFIG_RGBKBD_DEMO_FLOW)
	RGBKBD_DEMO_FLOW
#elif defined(CONFIG_RGBKBD_DEMO_DOT)
	RGBKBD_DEMO_DOT
#else
	RGBKBD_DEMO_OFF
#endif
	;

const int default_demo_interval_ms = 250;
test_export_static int demo_interval_ms = -1;

test_export_static
uint8_t rgbkbd_table[EC_RGBKBD_MAX_KEY_COUNT];

static enum rgbkbd_state rgbkbd_state;

const struct rgbkbd_init rgbkbd_default = {
	.gcc = RGBKBD_MAX_GCC_LEVEL / 2,
	.scale = RGBKBD_MAX_SCALE,
	.color = { .r = 0xff, .g = 0xff, .b = 0xff },  /* white */
};

static int set_color_single(struct rgb_s color, int x, int y)
{
	struct rgbkbd *ctx = &rgbkbds[0];
	uint8_t grid;
	uint8_t col = 0;
	uint8_t offset;
	int rv;

	if (rgbkbd_hsize <= x || rgbkbd_vsize <= y) {
		return EC_ERROR_OVERFLOW;
	}

	/* Search the grid where x belongs to. */
	for (grid = 0; grid < rgbkbd_count; grid++, ctx++) {
		if (x < col + ctx->cfg->col_len)
			break;
		col += ctx->cfg->col_len;
	}

	offset = ctx->cfg->row_len * (x - col) + y;
	ctx->buf[offset] = color;

	rv = ctx->cfg->drv->set_color(ctx, offset, &ctx->buf[offset], 1);

	CPRINTS("%set (%d,%d) to color=(%d,%d,%d) grid=%u offset=%u (%d)",
		rv ? "Failed to s" : "S",
		x, y, color.r, color.g, color.b, grid, offset, rv);

	return rv;
}

test_export_static uint8_t get_grid_size(const struct rgbkbd *ctx)
{
	return ctx->cfg->col_len * ctx->cfg->row_len;
}

static void sync_grids(void)
{
	struct rgbkbd *ctx;
	uint8_t len;
	int i;

	for (i = 0; i < rgbkbd_count; i++) {
		ctx = &rgbkbds[i];
		len = get_grid_size(ctx);
		ctx->cfg->drv->set_color(ctx, 0, ctx->buf, len);
	}
}

test_export_static struct rgb_s rotate_color(struct rgb_s color, int step)
{
	color.r += step;
	if (color.r == 0) {
		color.g += step;
		if (color.g == 0) {
			color.b += step;
		}
	}

	return color;
}

static void rgbkbd_reset_color(struct rgb_s color)
{
	struct rgbkbd *ctx;
	int i, j;

	for (i = 0; i < rgbkbd_count; i++) {
		ctx = &rgbkbds[i];
		for (j = 0; j < get_grid_size(ctx); j++)
			ctx->buf[j] = color;
	}

	sync_grids();
}

static void rgbkbd_demo_flow(void)
{
	struct rgbkbd *ctx = &rgbkbds[0];
	static struct rgb_s color;
	uint8_t len;
	int i, g;

	for (g = rgbkbd_count - 1; g >= 0; g--) {
		ctx = &rgbkbds[g];
		len = get_grid_size(ctx);
		for (i = len - 1; i > 0; i--)
			ctx->buf[i] = ctx->buf[i - 1];
		if (g > 0) {
			/* Copy the last dot of the g-1 grid to the 1st. */
			len = get_grid_size(&rgbkbds[g - 1]);
			ctx->buf[0] = rgbkbds[g - 1].buf[len - 1];
		}
	}

	/* Create a new color by shifting R by <step>. */
	color = rotate_color(color, 32);

	/* Finally, insert a new color to (0, 0). */
	ctx->buf[0] = color;

	sync_grids();

#ifdef TEST_BUILD
	task_wake(TASK_ID_TEST_RUNNER);
#endif
}

static void rgbkbd_demo_dot(void)
{
	static struct rgb_s color = { 0x80, 0, 0 };
	const struct rgb_s off = { 0, 0, 0 };
	static uint8_t x, y;

	/* Turn off previous dot. */
	set_color_single(off, x, y);

	/* Move position. */
	y++;
	if (y >= rgbkbd_vsize) {
		y = 0;
		x++;
		if (x >= rgbkbd_hsize) {
			x = 0;
			color = rotate_color(color, 0x80);
		}
	}

	/* Turn on next dot. */
	set_color_single(color, x, y);

#ifdef TEST_BUILD
	task_wake(TASK_ID_TEST_RUNNER);
#endif
}

static void rgbkbd_demo_run(enum rgbkbd_demo id)
{
	switch (id) {
	case RGBKBD_DEMO_FLOW:
		rgbkbd_demo_flow();
		break;
	case RGBKBD_DEMO_DOT:
		rgbkbd_demo_dot();
		break;
	case RGBKBD_DEMO_OFF:
	default:
		break;
	}
}

test_export_static
void rgbkbd_init_lookup_table(void)
{
	bool add = true;
	int i, k = 0;

	if (rgbkbd_map[0] != RGBKBD_DELM ||
			rgbkbd_map[rgbkbd_map_size - 1] != RGBKBD_DELM) {
		CPRINTS("Invalid Key-LED map");
		return;
	}

	/*
	 * rgbkbd_map[] consists of LED IDs separated by a delimiter (0xff).
	 * When 'add' is true, the next byte will be the beginning of a new LED
	 * group, thus, its index will be added to rgbkbd_table. If the next
	 * byte is a back-to-back 0xff, it's an empty group and still added to
	 * rgbkbd_table.
	 */
	for (i = 0; i < rgbkbd_map_size && k < EC_RGBKBD_MAX_KEY_COUNT; i++) {
		if (rgbkbd_map[i] != RGBKBD_DELM) {
			if (add)
				rgbkbd_table[k++] = i;
			/* Don't add next LED ID or TERM. */
			add = false;
			continue;
		}
		if (add)
			rgbkbd_table[k++] = i;
		add = true;
	}

	/* A valid map should have exactly as many entries as MAX_KEY_ID. */
	if (k < EC_RGBKBD_MAX_KEY_COUNT)
		CPRINTS("Key-LED map is too short (found %d)", k);

	/*
	 * Whether k is equal to or shorter than EC_RGBKBD_MAX_KEY_COUNT, the
	 * LED group pointed by rgbkbd_table[k-1] is guaranteed to be properly
	 * terminated. The rest of the table entries remain non-existent (0).
	 */
}

static int rgbkbd_set_global_brightness(uint8_t gcc)
{
	int e, grid;
	int rv = EC_SUCCESS;

	for (grid = 0; grid < rgbkbd_count; grid++) {
		struct rgbkbd *ctx = &rgbkbds[grid];

		e = ctx->cfg->drv->set_gcc(ctx, gcc);
		if (e) {
			CPRINTS("Failed to set GCC to %u for grid=%d (%d)",
				gcc, grid, e);
			rv = e;
			continue;
		}
	}

	CPRINTS("Set GCC to %u", gcc);

	/* Return EC_SUCCESS or the last error. */
	return rv;
}

static int rgbkbd_set_scale(uint8_t scale)
{
	int e, i, rv = EC_SUCCESS;

	for (i = 0; i < rgbkbd_count; i++) {
		struct rgbkbd *ctx = &rgbkbds[i];

		e = ctx->cfg->drv->set_scale(ctx, 0, scale, get_grid_size(ctx));
		if (e) {
			CPRINTS("Failed to set scale of GRID%d to %d (%d)",
				i, scale, e);
			rv = e;
		}
	}

	return rv;
}

static int rgbkbd_init(void)
{
	int rv = EC_SUCCESS;
	int e, i;

	for (i = 0; i < rgbkbd_count; i++) {
		struct rgbkbd *ctx = &rgbkbds[i];
		uint8_t scale = ctx->init->scale;
		uint8_t gcc = ctx->init->gcc;

		e = ctx->cfg->drv->init(ctx);
		if (e) {
			CPRINTS("Failed to init GRID%d (%d)", i, e);
			rv = e;
			continue;
		}

		e = ctx->cfg->drv->set_scale(ctx, 0, scale, get_grid_size(ctx));
		if (e) {
			CPRINTS("Failed to set scale of GRID%d to %d (%d)",
				i, scale, rv);
			rv = e;
			continue;
		}

		e = ctx->cfg->drv->set_gcc(ctx, gcc);
		if (e) {
			CPRINTS("Failed to set GCC to %u for grid=%d (%d)",
				gcc, i, e);
			rv = e;
			continue;
		}

		rgbkbd_reset_color(ctx->init->color);

		CPRINTS("Initialized GRID%d", i);
	}

	if (rv == EC_SUCCESS)
		rgbkbd_state = RGBKBD_STATE_INITIALIZED;

	return rv;
}

/* This is used to re-init on the first enable. */
static bool reinitialized;
static int rgbkbd_late_init(void)
{
	if (IS_ENABLED(CONFIG_IS31FL3743B_LATE_INIT)) {
		if (!reinitialized) {
			int rv;

			CPRINTS("Re-initializing");
			rv = rgbkbd_init();
			if (rv)
				return rv;
			reinitialized = true;
		}
	}
	return EC_SUCCESS;
}

static int rgbkbd_enable(int enable)
{
	int rv = EC_SUCCESS;
	int e, i;

	if (enable) {
		if (rgbkbd_state == RGBKBD_STATE_ENABLED)
			return EC_SUCCESS;
		rv = rgbkbd_late_init();
		if (rv)
			return rv;
	} else {
		if (rgbkbd_state == RGBKBD_STATE_DISABLED)
			return EC_SUCCESS;
	}

	for (i = 0; i < rgbkbd_count; i++) {
		struct rgbkbd *ctx = &rgbkbds[i];

		e = ctx->cfg->drv->enable(ctx, enable);
		if (e) {
			CPRINTS("Failed to %s GRID%d (%d)",
				enable ? "enable" : "disable", i, e);
			rv = e;
			continue;
		}

		CPRINTS("%s GRID%d", enable ? "Enabled" : "Disabled", i);
	}

	if (rv == EC_SUCCESS) {
		rgbkbd_state = enable ?
				RGBKBD_STATE_ENABLED : RGBKBD_STATE_DISABLED;
	}

	/* Return EC_SUCCESS or the last error. */
	return rv;
}

static void rgbkbd_demo_set(enum rgbkbd_demo new_demo)
{
	CPRINTS("Setting demo %d with %d ms interval", demo, demo_interval_ms);

	demo = new_demo;

	/* suspend demo task */
	demo_interval_ms = -1;
	rgbkbd_init();
	rgbkbd_enable(1);

	if (demo == RGBKBD_DEMO_OFF)
		return;

	demo_interval_ms = default_demo_interval_ms;

	/* start demo */
	task_wake(TASK_ID_RGBKBD);
}

static int rgbkbd_kblight_set(int percent)
{
	uint8_t gcc = DIV_ROUND_NEAREST(percent * RGBKBD_MAX_GCC_LEVEL, 100);
	int rv = rgbkbd_late_init();

	if (rv)
		return rv;

	return rgbkbd_set_global_brightness(gcc);
}

static int rgbkbd_get_enabled(void)
{
	return rgbkbd_state >= RGBKBD_STATE_ENABLED;
}

static void rgbkbd_reset(void)
{
	board_kblight_shutdown();
	board_kblight_init();
	rgbkbd_state = RGBKBD_STATE_RESET;
	reinitialized = false;
}

const struct kblight_drv kblight_rgbkbd = {
	.init = rgbkbd_init,
	.set = rgbkbd_kblight_set,
	.get = NULL,
	.enable = rgbkbd_enable,
	.get_enabled = rgbkbd_get_enabled,
};

void rgbkbd_task(void *u)
{
	rgbkbd_init_lookup_table();

	while (1) {
		task_wait_event(demo_interval_ms * MSEC);
		if (demo)
			rgbkbd_demo_run(demo);
	}
}

static enum ec_status hc_rgbkbd_set_color(struct host_cmd_handler_args *args)
{
	const struct ec_params_rgbkbd_set_color *p = args->params;
	int i;

	if (p->start_key + p->length > EC_RGBKBD_MAX_KEY_COUNT)
		return EC_RES_INVALID_PARAM;

	if (rgbkbd_late_init())
		return EC_RES_ERROR;

	for (i = 0; i < p->length; i++) {
		uint8_t j = rgbkbd_table[p->start_key + i];
		union rgbkbd_coord_u8 led;

		if (j == RGBKBD_NONE)
			/* Null or uninitialized entry */
			continue;

		do {
			led.u8 = rgbkbd_map[j++];
			if (led.u8 == RGBKBD_DELM)
				/* Reached end of the group. */
				break;
			if (set_color_single(p->color[i],
					     led.coord.x, led.coord.y))
				return EC_RES_ERROR;
		} while (led.u8 != RGBKBD_DELM);
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RGBKBD_SET_COLOR, hc_rgbkbd_set_color,
		     EC_VER_MASK(0));

static enum ec_status hc_rgbkbd(struct host_cmd_handler_args *args)
{
	const struct ec_params_rgbkbd *p = args->params;
	enum ec_status rv = EC_RES_ERROR;

	if (rgbkbd_late_init())
		return EC_RES_ERROR;

	switch (p->subcmd) {
	case EC_RGBKBD_SUBCMD_CLEAR:
		rgbkbd_reset_color(p->color);
		rv = EC_RES_SUCCESS;
		break;
	default:
		rv = EC_RES_INVALID_PARAM;
		break;
	}

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_RGBKBD, hc_rgbkbd, EC_VER_MASK(0));

test_export_static int cc_rgb(int argc, char **argv)
{
	char *end, *comma;
	struct rgb_s color;
	int gcc, x, y, val;
	int i, rv = EC_SUCCESS;

	if (argc < 2 || 5 < argc) {
		return EC_ERROR_PARAM_COUNT;
	}

	comma = strstr(argv[1], ",");
	if (comma && strlen(comma) > 1) {
		/* Usage 2 */
		/* Found ',' and more string after that. Split it into two. */
		*comma = '\0';
		x = strtoi(argv[1], &end, 0);
		if (*end || x >= rgbkbd_hsize)
			return EC_ERROR_PARAM1;
		y = strtoi(comma + 1, &end, 0);
		if (*end || y >= rgbkbd_vsize)
			return EC_ERROR_PARAM1;
	} else if (!strcasecmp(argv[1], "all")) {
		/* Usage 3 */
		x = -1;
		y = -1;
	} else if (!strcasecmp(argv[1], "demo")) {
		/* Usage 4 */
		val = strtoi(argv[2], &end, 0);
		if (*end || val >= RGBKBD_DEMO_COUNT)
			return EC_ERROR_PARAM1;
		rgbkbd_demo_set(val);
		return EC_SUCCESS;
	} else if (!strcasecmp(argv[1], "reset")) {
		rgbkbd_reset();
		rv = rgbkbd_init();
		if (rv)
			return rv;
		return rgbkbd_enable(0);
	} else if (!strcasecmp(argv[1], "enable")) {
		return rgbkbd_enable(1);
	} else if (!strcasecmp(argv[1], "disable")) {
		return rgbkbd_enable(0);
	} else if (!strcasecmp(argv[1], "scale")) {
		/* Usage 6 */
		val = strtoi(argv[2], &end, 0);
		if (*end || val > RGBKBD_MAX_SCALE)
			return EC_ERROR_PARAM2;
		return rgbkbd_set_scale(val);
	} else if (!strcasecmp(argv[1], "red")) {
		color.r = 255;
		color.g = 0;
		color.b = 0;
		rgbkbd_reset_color(color);
		return EC_SUCCESS;
	} else {
		/* Usage 1 */
		if (argc != 2)
			return EC_ERROR_PARAM_COUNT;
		gcc = strtoi(argv[1], &end, 0);
		if (*end || gcc < 0 || gcc > UINT8_MAX)
			return EC_ERROR_PARAM1;
		return rgbkbd_set_global_brightness(gcc);
	}

	if (argc != 5)
		return EC_ERROR_PARAM_COUNT;

	val = strtoi(argv[2], &end, 0);
	if (*end || val < 0 || val > UINT8_MAX)
		return EC_ERROR_PARAM2;
	color.r = val;
	val = strtoi(argv[3], &end, 0);
	if (*end || val < 0 || val > UINT8_MAX)
		return EC_ERROR_PARAM3;
	color.g = val;
	val = strtoi(argv[4], &end, 0);
	if (*end || val < 0 || val > UINT8_MAX)
		return EC_ERROR_PARAM4;
	color.b = val;

	rgbkbd_demo_set(RGBKBD_DEMO_OFF);
	if (y < 0 && x < 0) {
		/* Usage 3 */
		rgbkbd_reset_color(color);
	} else if (y < 0) {
		/* Usage 2: Set all LEDs on column x. */
		ccprintf("Set column %d to 0x%02x%02x%02x\n",
			 x, color.r, color.g, color.b);
		for (i = 0; i < rgbkbd_vsize; i++)
			rv = set_color_single(color, x, i);
	} else if (x < 0) {
		/* Usage 2: Set all LEDs on row y. */
		ccprintf("Set row %d to 0x%02x%02x%02x\n",
			 y, color.r, color.g, color.b);
		for (i = 0; i < rgbkbd_hsize; i++)
			rv = set_color_single(color, i, y);
	} else {
		/* Usage 2 */
		ccprintf("Set (%d,%d) to 0x%02x%02x%02x\n",
			 x, y, color.r, color.g, color.b);
		rv = set_color_single(color, x, y);
	}

	return rv;
}
#ifndef TEST_BUILD
DECLARE_CONSOLE_COMMAND(rgb, cc_rgb,
			"\n"
			"1. rgbk <global-brightness>\n"
			"2. rgbk <col,row> <r-bright> <g-bright> <b-bright>\n"
			"3. rgbk all <r-bright> <g-bright> <b-bright>\n"
			"4. rgbk demo <id>\n"
			"5. rgbk reset/enable/disable/red\n"
			"6. rgbk scale <val>\n",
			"Set color of RGB keyboard"
			);
#endif
