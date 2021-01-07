/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific configuration */
#include "adc_chip.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charger.h"
#include "charge_ramp.h"
#include "cros_board_info.h"
#include "driver/charger/isl9241.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/temp_sensor/thermistor.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "power/icelake.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#ifdef CONFIG_ZEPHYR
#include "usbc_config.h"
#endif

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/******************************************************************************/
/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_CHARGER] = {
		.name = "TEMP_CHARGER",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_PP3300_REGULATOR] = {
		.name = "TEMP_PP3300_REGULATOR",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH8,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4_FAN] = {
		.name = "TEMP_FAN",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_ACOK_OD,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/*
 * PWROK signal configuration, see the PWROK Generation Flow Diagram (Figure
 * 235) in the Tiger Lake Platform Design Guide for the list of potential
 * signals.
 *
 * Volteer uses this power sequence:
 *	GPIO_EN_PPVAR_VCCIN - Turns on the VCCIN rail. Also used as a delay to
 *		the VCCST_PWRGD input to the AP so this signal must be delayed
 *		5 ms to meet the tCPU00 timing requirement.
 *	GPIO_EC_PCH_SYS_PWROK - Asserts the SYS_PWROK input to the AP. Delayed
 *		a total of 50 ms after ALL_SYS_PWRGD input is asserted. See
 *		b/144478941 for full discussion.
 *
 * Volteer does not provide direct EC control for the VCCST_PWRGD and PCH_PWROK
 * signals. If your board adds these signals to the EC, copy this array
 * to your board.c file and modify as needed.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {
	{
		.gpio = GPIO_EN_PPVAR_VCCIN,
		.delay_ms = 5,
	},
	{
		.gpio = GPIO_EC_PCH_SYS_PWROK,
		.delay_ms = 50 - 5,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	/* No delays needed during S0 exit */
	{
		.gpio = GPIO_EC_PCH_SYS_PWROK,
	},
	/* Turn off VCCIN last */
	{
		.gpio = GPIO_EN_PPVAR_VCCIN,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_deassert_list);

/******************************************************************************/
/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_CHARGER] = {.name = "Charger",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_1_CHARGER},
	[TEMP_SENSOR_2_PP3300_REGULATOR] = {.name = "PP3300 Regulator",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_2_PP3300_REGULATOR},
	[TEMP_SENSOR_3_DDR_SOC] = {.name = "DDR and SOC",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_3_DDR_SOC},
	[TEMP_SENSOR_4_FAN] = {.name = "Fan",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_4_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static void baseboard_init(void)
{
	/* Enable monitoring of the PROCHOT input to the EC */
	gpio_enable_interrupt(GPIO_EC_PROCHOT_IN_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT);

static uint8_t board_id;

uint8_t get_board_id(void)
{
	return board_id;
}

__overridable void board_cbi_init(void)
{
}

/*
 * Read CBI from i2c eeprom and initialize variables for board variants
 *
 * Example for configuring for a USB3 DB:
 *   ectool cbi set 6 2 4 10
 */
static void cbi_init(void)
{
	uint32_t cbi_val;

	/* Board ID */
	if (cbi_get_board_version(&cbi_val) != EC_SUCCESS ||
	    cbi_val > UINT8_MAX)
		CPRINTS("CBI: Read Board ID failed");
	else
		board_id = cbi_val;

	CPRINTS("Board ID: %d", board_id);

	/* FW config */
	init_fw_config();

	/* Allow the board project to make runtime changes based on CBI data */
	board_cbi_init();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_FIRST);

