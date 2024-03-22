/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chronicler board-specific configuration */
#include "accelgyro.h"
#include "battery.h"
#include "battery_smart.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charge_state.h"
#include "common.h"
#include "driver/sync.h"
#include "driver/tcpm/ps8xxx.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "throttle_ap.h"
#include "uart.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3000,
	.rpm_start = 5000,
	.rpm_max = 5100,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

/******************************************************************************/
/* EC thermal management configuration */

/*
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (80 C)
 */
const static struct ec_thermal_config thermal_config_without_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

const static struct ec_thermal_config thermal_config_with_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	/* For real temperature fan_table (0 ~ 99C) */
	.temp_fan_off = C_TO_K(0),
	.temp_fan_max = C_TO_K(99),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_CHARGER] = thermal_config_with_fan,
	[TEMP_SENSOR_2_PP3300_REGULATOR] = thermal_config_without_fan,
	[TEMP_SENSOR_3_DDR_SOC] = thermal_config_without_fan,
	[TEMP_SENSOR_4_FAN] = thermal_config_without_fan,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

struct fan_step {
	int on;
	int off;
	int rpm;
};

/* Fan control table */
static const struct fan_step fan_table0[] = {
	{ .on = 30, .off = 0, .rpm = 3150 }, /* Fan level 0 */
	{ .on = 47, .off = 43, .rpm = 3500 }, /* Fan level 1 */
	{ .on = 50, .off = 47, .rpm = 3750 }, /* Fan level 2 */
	{ .on = 53, .off = 50, .rpm = 4200 }, /* Fan level 3 */
	{ .on = 56, .off = 53, .rpm = 4500 }, /* Fan level 4 */
	{ .on = 59, .off = 56, .rpm = 5000 }, /* Fan level 5 */
};

/* All fan tables must have the same number of levels */
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table0)

static const struct fan_step *fan_table = fan_table0;

#define FAN_AVERAGE_TIME_SEC 5

int fan_percent_to_rpm(int fan, int pct)
{
	static int current_level;
	static int previous_level = NUM_FAN_LEVELS;
	static int cnt, avg_pct, previous_pct;
	int i;

	/* Average several times to smooth fan rotating speed. */
	avg_pct += pct;

	if (++cnt != FAN_AVERAGE_TIME_SEC)
		return fan_table[previous_level].rpm;

	avg_pct = (int)avg_pct / FAN_AVERAGE_TIME_SEC;

	/*
	 * Compare the pct and previous pct, we have the three paths :
	 *  1. decreasing path. (check the off point)
	 *  2. increasing path. (check the on point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (avg_pct < previous_pct) {
		for (i = current_level; i >= 0; i--) {
			if (avg_pct <= fan_table[i].off)
				current_level = i - 1;
			else
				break;
		}
	} else if (avg_pct > previous_pct) {
		for (i = current_level + 1; i < NUM_FAN_LEVELS; i++) {
			if (avg_pct >= fan_table[i].on)
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level != previous_level)
		cprints(CC_THERMAL, "Setting fan RPM to %d",
			fan_table[current_level].rpm);

	previous_pct = avg_pct;
	previous_level = current_level;

	cnt = 0;
	avg_pct = 0;

	return fan_table[current_level].rpm;
}

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "usb_c0",
		.port = I2C_PORT_USB_C0,
		.kbps = 1000,
		.scl = GPIO_EC_I2C1_USB_C0_SCL,
		.sda = GPIO_EC_I2C1_USB_C0_SDA,
	},
	{
		.name = "usb_c1",
		.port = I2C_PORT_USB_C1,
		.kbps = 1000,
		.scl = GPIO_EC_I2C2_USB_C1_SCL,
		.sda = GPIO_EC_I2C2_USB_C1_SDA,
	},
	{
		.name = "usb_1_mix",
		.port = I2C_PORT_USB_1_MIX,
		.kbps = 100,
		.scl = GPIO_EC_I2C3_USB_1_MIX_SCL,
		.sda = GPIO_EC_I2C3_USB_1_MIX_SDA,
	},
	{
		.name = "power",
		.port = I2C_PORT_POWER,
		.kbps = 100,
		.scl = GPIO_EC_I2C5_BATTERY_SCL,
		.sda = GPIO_EC_I2C5_BATTERY_SDA,
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C7_EEPROM_PWR_SCL_R,
		.sda = GPIO_EC_I2C7_EEPROM_PWR_SDA_R,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* PWM configuration */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 5,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = 0,
		/*
		 * Set PWM frequency to multiple of 50 Hz and 60 Hz to prevent
		 * flicker. Higher frequencies consume similar average power to
		 * lower PWM frequencies, but higher frequencies record a much
		 * lower maximum power.
		 */
		.freq = 2400,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* keyboard config */
static const struct ec_response_keybd_config main_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		/*
		 *  Chronicler keyboard swaps T2 and T3 in the keyboard
		 *  matrix,So swap the actions key lookup to match.
		 *  The physical keyboard still orders the top row as
		 *  Back, Refresh, Fullscreen, etc.
		 */
		TK_FULLSCREEN,		/* T3 */
		TK_REFRESH,		/* T2 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &main_kb;
}

/******************************************************************************/
/* keyboard factory test */
#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */

const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 }, { 1, 1 },	  { 1, 0 },   { 0, 6 },
	{ 0, 7 },   { 1, 4 }, { 1, 3 },	  { 1, 6 },   { 1, 7 },
	{ 3, 1 },   { 2, 0 }, { 1, 5 },	  { 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 }, { 2, 5 },	  { 1, 2 },   { 2, 3 },
	{ 2, 2 },   { 3, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

/******************************************************************************/
/* drop battery charging voltage depending on battery run time */

#ifdef BATTERY_RUNTIME_TEST
static int manual_run_time = -1;
#endif

struct drop_step {
	int run_time; /* battery run time (day) */
	int drop_volt; /* drop voltage (mV) */
};

/* voltage drop table */
static const struct drop_step voltage_drop_table[] = {
	{ .run_time = 90, .drop_volt = 13200 }, /* drop level 0 */
	{ .run_time = 198, .drop_volt = 13125 }, /* drop level 1 */
	{ .run_time = 305, .drop_volt = 13050 }, /* drop level 2 */
	{ .run_time = 412, .drop_volt = 12975 }, /* drop level 3 */
	{ .run_time = 519, .drop_volt = 12900 }, /* drop level 4 */
	{ .run_time = 626, .drop_volt = 12825 }, /* drop level 5 */
	{ .run_time = __INT_MAX__, .drop_volt = 12750 }, /* drop level 6 */
};

#define NUM_DROP_LEVELS ARRAY_SIZE(voltage_drop_table)

static int get_battery_run_time_day(uint32_t *battery_run_time)
{
	int rv;
	uint32_t run_time;
	uint8_t data[6];

	/* get battery run time */
	rv = sb_read_mfgacc(PARAM_FIRMWARE_RUNTIME, SB_ALT_MANUFACTURER_ACCESS,
			    data, sizeof(data));

	if (rv)
		return EC_ERROR_UNKNOWN;
	/*
	 * The response is 6 bytes; the runtime in seconds is the last 4 bytes.
	 */
	run_time = *(int32_t *)(&data[2]);

#ifdef BATTERY_RUNTIME_TEST
	cprints(CC_CHARGER, "run_time : 0x%08x (%d day)", run_time,
		(run_time / 86400));

	/* manual battery run time fot test */
	if (manual_run_time != -1)
		run_time = manual_run_time;
#endif
	/* second to day */
	*battery_run_time = run_time / 86400;

	return EC_SUCCESS;
}

/* charger profile override */
int charger_profile_override(struct charge_state_data *curr)
{
	int i;
	uint32_t batt_run_time = 0;

	/* Should not do anything if battery not present */
	if (battery_hw_present() != BP_YES)
		return 0;

	if (get_battery_run_time_day(&batt_run_time))
		return EC_ERROR_UNKNOWN;

	for (i = 0; i < NUM_DROP_LEVELS; i++) {
		if (batt_run_time <= voltage_drop_table[i].run_time)
			break;
	}

	curr->requested_voltage =
		MIN(curr->requested_voltage, voltage_drop_table[i].drop_volt);
#ifdef BATTERY_RUNTIME_TEST
	cprints(CC_CHARGER,
		"Charger: run time(day): %d, drop level: %d, CV: %d",
		batt_run_time, i, curr->requested_voltage);
#endif
	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}

static void battery_runtime_init(void)
{
	uint32_t batt_run_time = 0;

	/* Should not do anything if battery not present */
	if (battery_hw_present() != BP_YES)
		return;

	if (get_battery_run_time_day(&batt_run_time))
		return;

	cprints(CC_CHARGER, "battery run time(day): %d", batt_run_time);
}
DECLARE_HOOK(HOOK_INIT, battery_runtime_init, HOOK_PRIO_LAST);

#ifdef BATTERY_RUNTIME_TEST
/* test command */
static int command_manual_run_time(int argc, const char **argv)
{
	char *e = NULL;

	if (argc < 2) {
		manual_run_time = -1;
		cprints(CC_CHARGER, "manual run time reset");
		return EC_SUCCESS;
	}

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	manual_run_time = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	cprints(CC_CHARGER, "manual run time set to %d sec (%d day)",
		manual_run_time, (manual_run_time / 86400));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rt, command_manual_run_time, "<battery_run_time_sec>",
			"Set manual run time for test");
#endif
