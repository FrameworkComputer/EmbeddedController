/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Board re-init for Telith board.
 * Telith has only clamshell config.
 */
#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "cros_cbi.h"
#include "fan.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_interface.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

#define INT_RECHECK_US 5000

static void check_audio_jack(void);
DECLARE_DEFERRED(check_audio_jack);

static void check_audio_jack(void)
{
	int value;
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_jack_detect_l))) {
			value = 0;
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_loading_enable_l),
				value);
		} else {
			value = 1;
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_loading_enable_l),
				value);
		}
	} else {
		value = 0;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_loading_enable_l),
				value);
	}
}

void audio_jack_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&check_audio_jack_data, INT_RECHECK_US);
}

static void jack_detect_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_jack_detect));
}
DECLARE_HOOK(HOOK_INIT, jack_detect_init, HOOK_PRIO_DEFAULT);

static void check_audio_jack_handler(struct ap_power_ev_callback *cb,
				     struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		/* Called on AP S3 -> S0 transition */
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_jack_detect_l))) {
			value = 0;
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_loading_enable_l),
				value);
		} else {
			value = 1;
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_loading_enable_l),
				value);
		}
		break;

	case AP_POWER_SUSPEND:
	case AP_POWER_SHUTDOWN:
		value = 0;
		/* Called on AP S0 -> S3 transition */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_loading_enable_l),
				value);
		break;
	}
}

static int jack_detect_handler(void)
{
	static struct ap_power_ev_callback cb;
	/*
	 * Add a callback for suspend/resume/shutdowm to
	 * control the jack_detect VCC.
	 */
	ap_power_ev_init_callback(&cb, check_audio_jack_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND |
					  AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	return 0;
}
SYS_INIT(jack_detect_handler, APPLICATION, 1);

enum battery_present battery_hw_present(void)
{
	const struct gpio_dt_spec *batt_pres;

	batt_pres = GPIO_DT_FROM_NODELABEL(gpio_ec_battery_pres_odl);

	/* The GPIO is low when the battery is physically present */
	return gpio_pin_get_dt(batt_pres) ? BP_NO : BP_YES;
}

/* thermal tabel control for 15W CPU */
#define TEMP_VR TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_vr))
#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_cpu))
#define TEMP_AMBIENT TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_ambient))
#define TEMP_CHARGER TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_charger))

const static struct ec_thermal_config thermal_vr = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(88),
		[EC_TEMP_THRESH_HALT] = C_TO_K(91),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(43),
	.temp_fan_max = C_TO_K(72),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(79),
		[EC_TEMP_THRESH_HALT] = C_TO_K(82),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(76),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(38),
	.temp_fan_max = C_TO_K(66),
};

const static struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(78),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(72),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(36),
	.temp_fan_max = C_TO_K(68),
};

const static struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(82),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(41),
	.temp_fan_max = C_TO_K(73),
};

test_export_static void fan_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_FAN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_FAN);
		return;
	}
	if (val != FW_FAN_PRESENT) {
		/* Disable the fan */
		fan_set_count(0);
	} else {
		/* Configure the fan enable GPIO */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_fan),
			GPIO_OUTPUT);
		thermal_params[TEMP_VR] = thermal_vr;
		thermal_params[TEMP_CPU] = thermal_cpu;
		thermal_params[TEMP_AMBIENT] = thermal_ambient;
		thermal_params[TEMP_CHARGER] = thermal_charger;
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);

static bool key_bl = FW_KB_BL_NOT_PRESENT;
static bool key_numpad = FW_KB_NUMPAD_NOT_PRESENT;
/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_BL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_BL);
		return;
	}

	if (val == FW_KB_BL_PRESENT) {
		key_bl = FW_KB_BL_PRESENT;
	} else {
		key_bl = FW_KB_BL_NOT_PRESENT;
	}

	ret = cros_cbi_get_fw_config(FW_KB_NUMPAD, &val);
	if (val == FW_KB_NUMPAD_PRESENT) {
		key_numpad = FW_KB_NUMPAD_PRESENT;
	} else {
		key_numpad = FW_KB_NUMPAD_NOT_PRESENT;
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_POST_FIRST);

int8_t board_vivaldi_keybd_idx(void)
{
	int kb_status;

	kb_status = (key_numpad << 1) | key_bl;

	switch (kb_status) {
	case 0:
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_0));
	case 1:
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_1));
	case 2:
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_2));
	case 3:
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_3));
	default:
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_0));
	}
}

/*
 * Enable interrupts
 */
static void board_init(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_s5), 1);

	if (ap_power_in_state(AP_POWER_STATE_ON)) {
		disable_sleep(SLEEP_MASK_AP_RUN);
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* for U8816 issue WA */
static void bl_pg_handle(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_blpwr_en), 1);
	k_msleep(50);
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_blpwr_pgd))) {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_blpwr_pgd));
	}
}
DECLARE_DEFERRED(bl_pg_handle);

void bl_pg_interrupt(enum gpio_signal s)
{
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_blpwr_pgd))) {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_blpwr_pgd));
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_blpwr_en), 0);
		hook_call_deferred(&bl_pg_handle_data, 10 * USEC_PER_MSEC);
	}
}

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_STARTUP:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_blpwr_en), 1);
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_blpwr_pgd));
		break;

	case AP_POWER_HARD_OFF:
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_blpwr_pgd));
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_blpwr_en), 0);
		break;
	}
}

static int install_backlight_handler(void)
{
	static struct ap_power_ev_callback cb;
	/*
	 * Add a callback for start/hardoff to
	 * control the backlight load swith.
	 */
	ap_power_ev_init_callback(&cb, board_backlight_handler,
				  AP_POWER_STARTUP | AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&cb);

	return 0;
}

SYS_INIT(install_backlight_handler, APPLICATION, 1);
