/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan_chip.h"
#include "fan.h"
#include "hooks.h"
#include "pwm.h"
#include "util.h"

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
	[MFT_CH_1] = {
		.module = NPCX_MFT_MODULE_2,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN2,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

static const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

static const struct fan_conf fan_conf_1 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_1, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN2,
};

/*
 * TODO(b/234545460): thermistor placement and calibration
 *
 * Prototype fan spins at about 4200 RPM at 100% PWM, this
 * is specific to board ID 2 and might also apears in later
 * boards as well.
 */
static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2500,
	.rpm_start = 2500,
	.rpm_max = 6000,
};

static const struct fan_rpm fan_rpm_1 = {
	.rpm_min = 2500,
	.rpm_start = 2500,
	.rpm_max = 6000,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
	[FAN_CH_1] = {
		.conf = &fan_conf_1,
		.rpm = &fan_rpm_1,
	},
};

/* fan control */

struct fan_step {
	int on;
	int off;
	int rpm;
};

struct fan_table_config {
	/* number of control_table */
	uint8_t step;
	/* fan control table */
	const struct fan_step *control_table;
};

const struct fan_step fan_table0[] = {
	{ .on = 25, .off = 0, .rpm = 0 },
	{ .on = 37, .off = 34, .rpm = 2500 },
	{ .on = 42, .off = 39, .rpm = 2800 },
	{ .on = 46, .off = 43, .rpm = 3000 },
	{ .on = 51, .off = 48, .rpm = 3200 },
	{ .on = 55, .off = 52, .rpm = 3600 },
	{ .on = 59, .off = 56, .rpm = 4000 },
	{ .on = 66, .off = 63, .rpm = 4600 },
	{ .on = 72, .off = 69, .rpm = 5000 },
	{ .on = 74, .off = 71, .rpm = 5500 },
};
const int fan_table0_count = ARRAY_SIZE(fan_table0);

const struct fan_step fan_table1[] = {
	{ .on = 25, .off = 0, .rpm = 0 },
	{ .on = 51, .off = 48, .rpm = 3200 },
	{ .on = 55, .off = 52, .rpm = 3600 },
	{ .on = 59, .off = 56, .rpm = 4000 },
	{ .on = 66, .off = 63, .rpm = 4600 },
	{ .on = 72, .off = 69, .rpm = 5000 },
	{ .on = 74, .off = 71, .rpm = 5500 },
};
const int fan_table1_count = ARRAY_SIZE(fan_table1);

/* Fan control configuration */
static struct fan_table_config fan_tables[] = {
	[FAN_CH_0] = {
		.step = fan_table0_count,
		.control_table = (const struct fan_step *) &fan_table0,
	},
	[FAN_CH_1] = {
		.step = fan_table1_count,
		.control_table = (const struct fan_step *) &fan_table1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fan_tables) == FAN_CH_COUNT);

static int current_level[] = { 0, 0 };
BUILD_ASSERT(ARRAY_SIZE(current_level) == FAN_CH_COUNT);

static int previous_level[] = { 0, 0 };
BUILD_ASSERT(ARRAY_SIZE(previous_level) == FAN_CH_COUNT);

#undef BOARD_FAN_TEST

#ifdef BOARD_FAN_TEST
static int manual_temp = -1;
#endif

int fan_percent_to_rpm(int fan, int pct)
{
	static struct fan_table_config *fan_table;
	static int previous_pct;
	int i;

	fan_table = &fan_tables[fan];

#ifdef BOARD_FAN_TEST
	if (manual_temp != -1)
		pct = manual_temp;
#endif

	/*
	 * Compare the pct and previous pct, we have the three paths :
	 *  1. decreasing path. (check the off point)
	 *  2. increasing path. (check the on point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (pct < previous_pct) {
		for (i = current_level[fan]; i >= 0; i--) {
			if (pct <= fan_table->control_table[i].off)
				current_level[fan] = i - 1;
			else
				break;
		}
	} else if (pct > previous_pct) {
		for (i = current_level[fan] + 1; i < fan_table->step; i++) {
			if (pct >= fan_table->control_table[i].on)
				current_level[fan] = i;
			else
				break;
		}
	}

	if (current_level[fan] < 0)
		current_level[fan] = 0;

	if (current_level[fan] != previous_level[fan])
		cprints(CC_THERMAL, "Fan %d: Set fan RPM to %d", fan,
			fan_table->control_table[current_level[fan]].rpm);

	if (fan == (FAN_CH_COUNT - 1))
		previous_pct = pct;

#ifdef BOARD_FAN_TEST
	if (manual_temp != -1)
		ccprints("Fan%d: temps:%d curr:%d prev:%d rpm:%d", fan, pct,
			 current_level[fan], previous_level[fan],
			 fan_table->control_table[current_level[fan]].rpm);
#endif

	previous_level[fan] = current_level[fan];

	return fan_table->control_table[current_level[fan]].rpm;
}

#ifdef BOARD_FAN_TEST
static int command_fan_test(int argc, const char **argv)
{
	char *e;
	int t;

	if (argc > 1) {
		t = strtoi(argv[1], &e, 0);
		if (*e) {
			ccprints("Invalid test temp");
			return EC_ERROR_INVAL;
		}
		manual_temp = t;
		ccprints("manual temp is %d", manual_temp);
		return EC_SUCCESS;
	}
	manual_temp = -1;
	ccprints("manual temp reset");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fan_test, command_fan_test, "[temperature]",
			"set manual temperature for fan test");
#endif
