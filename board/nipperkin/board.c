/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nipperkin board-specific configuration */

#include "adc.h"
#include "base_fw_config.h"
#include "battery.h"
#include "board_fw_config.h"
#include "button.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/charger/isl9241.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818_public.h"
#include "driver/temp_sensor/pct2075.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"
#include "timer.h"
#include "usb_mux.h"

static void hdmi_hpd_interrupt(enum gpio_signal signal);

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 }, { 1, 0 },   { 0, 6 },	  { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 }, { 1, 3 },   { -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 }, { 1, 5 },   { 2, 6 },	  { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 }, { 1, 2 },   { 2, 3 },	  { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { 0, 4 }, { -1, -1 }, { 8, 2 },	  { -1, -1 },
	{ -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

__override enum ec_error_list
board_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

__override int board_c1_ps8818_mux_set(const struct usb_mux *me,
				       mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_RX_PHY,
					      PS8818_RX_INPUT_TERM_MASK,
					      PS8818_RX_INPUT_TERM_112_OHM);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_DPEQ_LEVEL,
					      PS8818_DPEQ_LEVEL_UP_MASK,
					      PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable HPD on the DB */
		ioex_set_level(IOEX_USB_C1_IN_HPD, 1);
	} else {
		/* Disable HPD on the DB */
		ioex_set_level(IOEX_USB_C1_IN_HPD, 0);
	}

	return rv;
}

static void board_init(void)
{
	if (get_board_version() > 1)
		gpio_enable_interrupt(GPIO_HPD_EC_IN);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	if (get_board_version() > 1)
		pct2075_init();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

int board_get_soc_temp_k(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_k(idx, temp_k);
}

int board_get_soc_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_mk(PCT2075_SOC, temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_mk(PCT2075_AMB, temp_mk);
}

/* ADC Channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_MEMORY] = {
		.name = "MEMORY",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_CORE_IMON1] = {
		.name = "CORE_I",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SOC_IMON2] = {
		.name = "SOC_I",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temp Sensors */
static int board_get_temp(int, int *);

const struct pct2075_sensor_t pct2075_sensors[] = {
	{ I2C_PORT_SENSOR, PCT2075_I2C_ADDR_FLAGS0 },
	{ I2C_PORT_SENSOR, PCT2075_I2C_ADDR_FLAGS7 },
};
BUILD_ASSERT(ARRAY_SIZE(pct2075_sensors) == PCT2075_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_soc_temp_k,
		.idx = PCT2075_SOC,
	},
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_MEMORY] = {
		.name = "Memory",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = ADC_TEMP_SENSOR_MEMORY,
	},
	[TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = ADC_TEMP_SENSOR_5V_REGULATOR,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
	[TEMP_SENSOR_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = pct2075_get_val_k,
		.idx = PCT2075_AMB,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_SOC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
			[EC_TEMP_THRESH_HALT] = C_TO_K(83),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		},
	},
	[TEMP_SENSOR_CHARGER] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
			[EC_TEMP_THRESH_HALT] = C_TO_K(81),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(72),
		},
	},
	[TEMP_SENSOR_MEMORY] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
			[EC_TEMP_THRESH_HALT] = C_TO_K(83),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		},
	},
	[TEMP_SENSOR_5V_REGULATOR] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(55),
			[EC_TEMP_THRESH_HALT] = C_TO_K(58),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(47),
		},
	},
	[TEMP_SENSOR_CPU] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
	},
	/*
	 * Note: Leave ambient entries at 0, both as it does not represent a
	 * hotspot and as not all boards have this sensor
	 */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

static int board_get_temp(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;
	return get_temp_3v3_30k9_47k_4050b(idx, temp_k);
}

static int check_hdmi_hpd_status(void)
{
	if (get_board_version() > 1)
		return gpio_get_level(GPIO_HPD_EC_IN);
	else
		return true;
}

/* Called on AP resume to S0 */
static void board_chipset_resume(void)
{
	ioex_set_level(IOEX_USB_A1_PD_R_L, 1);
	ioex_set_level(IOEX_EN_PWR_HDMI, 1);
	ioex_set_level(IOEX_HDMI_DATA_EN, 1);
	crec_msleep(PI3HDX1204_POWER_ON_DELAY_MS);
	pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
			  check_hdmi_hpd_status());
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP suspend */
static void board_chipset_suspend(void)
{
	pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 0);
	ioex_set_level(IOEX_HDMI_DATA_EN, 0);
	ioex_set_level(IOEX_EN_PWR_HDMI, 0);
	ioex_set_level(IOEX_USB_A1_PD_R_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*
 * With privacy screen, with keyboard backlight
 */
static const struct ec_response_keybd_config
	keybd_w_privacy_w_kblight = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_PRIVACY_SCRN_TOGGLE,		/* T8 */
		TK_KBD_BKLIGHT_TOGGLE,		/* T9 */
		TK_MICMUTE,			/* T10 */
		TK_VOL_MUTE,			/* T11 */
		TK_VOL_DOWN,			/* T12 */
		TK_VOL_UP,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

/*
 * Without privacy screen, with keyboard backlight
 */
static const struct ec_response_keybd_config
	keybd_wo_privacy_w_kblight = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_KBD_BKLIGHT_TOGGLE,		/* T8 */
		TK_PLAY_PAUSE,			/* T9 */
		TK_MICMUTE,			/* T10 */
		TK_VOL_MUTE,			/* T11 */
		TK_VOL_DOWN,			/* T12 */
		TK_VOL_UP,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

/*
 * With privacy screen, without keyboard backlight
 */
static const struct ec_response_keybd_config
	keybd_w_privacy_wo_kblight = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_PRIVACY_SCRN_TOGGLE,		/* T8 */
		TK_PLAY_PAUSE,			/* T9 */
		TK_MICMUTE,			/* T10 */
		TK_VOL_MUTE,			/* T11 */
		TK_VOL_DOWN,			/* T12 */
		TK_VOL_UP,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

/*
 * Without privacy screen, without keyboard backlight
 */
static const struct ec_response_keybd_config
	keybd_wo_privacy_wo_kblight_V0 = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_PREV_TRACK,			/* T8 */
		TK_PLAY_PAUSE,			/* T9 */
		TK_MICMUTE,			/* T10 */
		TK_VOL_MUTE,			/* T11 */
		TK_VOL_DOWN,			/* T12 */
		TK_VOL_UP,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config
	keybd_wo_privacy_wo_kblight_V1 = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_PLAY_PAUSE,			/* T8 */
		TK_MICMUTE,			/* T9 */
		TK_VOL_MUTE,			/* T10 */
		TK_VOL_DOWN,			/* T11 */
		TK_VOL_UP,			/* T12 */
		TK_MENU,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (board_has_privacy_panel() && board_has_kblight())
		return &keybd_w_privacy_w_kblight;
	else if (!board_has_privacy_panel() && board_has_kblight())
		return &keybd_wo_privacy_w_kblight;
	else if (board_has_privacy_panel() && !board_has_kblight())
		return &keybd_w_privacy_wo_kblight;
	else {
		if (get_board_version() <= 3)
			return &keybd_wo_privacy_wo_kblight_V0;
		else
			return &keybd_wo_privacy_wo_kblight_V1;
	}
}

const struct pi3hdx1204_tuning pi3hdx1204_tuning = {
	.eq_ch0_ch1_offset = PI3HDX1204_EQ_DB710,
	.eq_ch2_ch3_offset = PI3HDX1204_EQ_DB710,
	.vod_offset = PI3HDX1204_VOD_115_ALL_CHANNELS,
	.de_offset = PI3HDX1204_DE_DB_MINUS5,
};

static void hdmi_hpd_handler(void)
{
	int hpd = check_hdmi_hpd_status();

	ccprints("HDMI HPD %d", hpd);
	pi3hdx1204_enable(
		I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) && hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* Debounce for 2 msec */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

void board_set_current_limit(void)
{
	const int no_battery_current_limit_override_ma = 6000;
	/*
	 * When there is no battery, override charger current limit to
	 * prevent brownout during boot.
	 */
	if (battery_is_present() == BP_NO) {
		ccprints("No Battery Found - Override Current Limit to %dmA",
			 no_battery_current_limit_override_ma);
		charger_set_input_current_limit(
			CHARGER_SOLO, no_battery_current_limit_override_ma);
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, board_set_current_limit,
	     HOOK_PRIO_INIT_EXTPOWER);

/* Set the DCPROCHOT base on battery over discharging current 5.888A */
static void set_dc_prochot(void)
{
	/*
	 * Only bits 13:8 are usable for this register, any other bits will be
	 * truncated.  Valid values are 256 mA to 16128 mA at 256 mA intervals.
	 */
	isl9241_set_dc_prochot(CHARGER_SOLO, 5888);
}
DECLARE_HOOK(HOOK_INIT, set_dc_prochot, HOOK_PRIO_DEFAULT);
