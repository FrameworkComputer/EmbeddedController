/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board-specific configuration */

#include "adc.h"
#include "base_fw_config.h"
#include "board_fw_config.h"
#include "button.h"
#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/temp_sensor/tmp112.h"
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
#include "temp_sensor/tmp112.h"
#include "thermal.h"
#include "usb_mux.h"

#include "gpio_list.h" /* Must come after other header files. */

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
	{0, 7}, {-1, -1}, {-1, -1}, {1, 4}, {1, 3},
	{-1, -1}, {1, 6}, {1, 7}, {3, 1}, {2, 0},
	{1, 5}, {2, 6}, {2, 7}, {2, 1}, {2, 4},
	{2, 5}, {1, 2}, {2, 3}, {2, 2}, {3, 0},
	{-1, -1}, {0, 4}, {-1, -1}, {8, 2}, {-1, -1},
	{-1, -1},
};
const int keyboard_factory_scan_pins_used =
		ARRAY_SIZE(keyboard_factory_scan_pins);

__override enum ec_error_list
board_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

/*
 * PS8818 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 * TODO(b/179036200): Adjust PS8818 tuning for guybrush reference
 */
__override int board_c1_ps8818_mux_set(const struct usb_mux *me,
				    mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_RX_PHY,
					PS8818_RX_INPUT_TERM_MASK,
					PS8818_RX_INPUT_TERM_112_OHM);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_DPEQ_LEVEL,
					PS8818_DPEQ_LEVEL_UP_MASK,
					PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 1);
	} else {
		/* Disable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 0);
	}

	return rv;
}

static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	if (get_board_version() > 1)
		tmp112_init();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup,
	     HOOK_PRIO_DEFAULT);

int board_get_soc_temp_k(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return tmp112_get_val_k(idx, temp_k);
}

int board_get_soc_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return tmp112_get_val_mk(TMP112_SOC, temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return tmp112_get_val_mk(TMP112_AMB, temp_mk);
}

/* ADC Channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
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
	[ADC_TEMP_SENSOR_MEMORY] = {
		.name = "MEMORY",
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
static int board_get_memory_temp(int, int *);

const struct tmp112_sensor_t tmp112_sensors[] = {
	{ I2C_PORT_SENSOR, TMP112_I2C_ADDR_FLAGS0 },
	{ I2C_PORT_SENSOR, TMP112_I2C_ADDR_FLAGS1 },
};
BUILD_ASSERT(ARRAY_SIZE(tmp112_sensors) == TMP112_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_soc_temp_k,
		.idx = TMP112_SOC,
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
		.read = board_get_memory_temp,
		.idx = ADC_TEMP_SENSOR_MEMORY,
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
		.read = tmp112_get_val_k,
		.idx = TMP112_AMB,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_SOC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		/* TODO: Setting fan off to 0 so it's allways on */
		.temp_fan_off = C_TO_K(0),
		.temp_fan_max = C_TO_K(70),
	},
	[TEMP_SENSOR_CHARGER] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
	[TEMP_SENSOR_MEMORY] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
	[TEMP_SENSOR_CPU] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		/*
		 * CPU temp sensor fan thresholds are high because they are a
		 * backup for the SOC temp sensor fan thresholds.
		 */
		.temp_fan_off = C_TO_K(60),
		.temp_fan_max = C_TO_K(90),
	},
	/*
	 * Note: Leave ambient entries at 0, both as it does not represent a
	 * hotspot and as not all boards have this sensor
	 */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

static int board_get_memory_temp(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;
	return get_temp_3v3_30k9_47k_4050b(idx, temp_k);
}
