/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Malefor
 */

#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ktd20xx.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "lid_switch.h"
#include "stdbool.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define LED_OFF_LVL	1
#define LED_ON_LVL	0

const int led_charge_lvl_1 = 5;

const int led_charge_lvl_2 = 97;

struct led_descriptor led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_RED, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_GREEN, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_DISCHARGE_S3]	     = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_RED,   1 * LED_ONE_SEC},
					{LED_OFF,	     1 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_RED,   2 * LED_ONE_SEC},
					{EC_LED_COLOR_GREEN, 2 * LED_ONE_SEC} },
};

const struct led_descriptor
		led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]            = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]    = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					 {LED_OFF,         3 * LED_ONE_SEC} },
	[PWR_LED_STATE_SUSPEND_NO_AC] = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					 {LED_OFF,         3 * LED_ONE_SEC} },
	[PWR_LED_STATE_OFF]           = {{LED_OFF,            LED_INDEFINITE} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_set_color_power(enum ec_led_colors color)
{
	if (color == EC_LED_COLOR_WHITE)
		gpio_set_level(GPIO_LED_3_L, LED_ON_LVL);
	else
		/* LED_OFF and unsupported colors */
		gpio_set_level(GPIO_LED_3_L, LED_OFF_LVL);
}

void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		gpio_set_level(GPIO_LED_1_L, LED_ON_LVL);
		gpio_set_level(GPIO_LED_2_L, LED_ON_LVL);
		break;
	case EC_LED_COLOR_RED:
		gpio_set_level(GPIO_LED_1_L, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_2_L, LED_ON_LVL);
		break;
	case EC_LED_COLOR_GREEN:
		gpio_set_level(GPIO_LED_1_L, LED_ON_LVL);
		gpio_set_level(GPIO_LED_2_L, LED_OFF_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_LED_1_L, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_2_L, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_GREEN] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_battery(EC_LED_COLOR_RED);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(EC_LED_COLOR_AMBER);
		else if (brightness[EC_LED_COLOR_GREEN] != 0)
			led_set_color_battery(EC_LED_COLOR_GREEN);
		else
			led_set_color_battery(LED_OFF);
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_power(EC_LED_COLOR_WHITE);
		else
			led_set_color_power(LED_OFF);
	}

	return EC_SUCCESS;
}

static const uint16_t lightbar_i2c_addr = 0x68;
static void controller_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2];

	buf[0] = reg;
	buf[1] = val;

	i2c_xfer_unlocked(I2C_PORT_LIGHTBAR, lightbar_i2c_addr,
			buf, 2, 0, 0,
			I2C_XFER_SINGLE);
}

enum lightbar_states {
	LB_STATE_OFF,
	LB_STATE_LID_CLOSE,
	LB_STATE_AC_ONLY,
	LB_STATE_AC_BAT_LOW,
	LB_STATE_AC_BAT_20,
	LB_STATE_AC_BAT_40,
	LB_STATE_AC_BAT_60,
	LB_STATE_AC_BAT_80,
	LB_STATE_AC_BAT_100,
	LB_STATE_BAT_LOW,
	LB_STATE_BAT_ONLY,
	LB_NUM_STATES
};

/*
 * All lightbar states should have one phase defined,
 * and an additional phase can be defined for blinking
 */
enum lightbar_phase {
	LIGHTBAR_PHASE_0 = 0,
	LIGHTBAR_PHASE_1 = 1,
	LIGHTBAR_NUM_PHASES
};

enum ec_lightbar_colors {
	BAR_RESET                    = 0x00,
	BAR_OFF                      = 0x01,
	BAR_COLOR_ORG_20_PERCENT     = 0x02,
	BAR_COLOR_ORG_40_PERCENT     = 0x03,
	BAR_COLOR_ORG_60_PERCENT     = 0x04,
	BAR_COLOR_ORG_80_PERCENT     = 0x05,
	BAR_COLOR_ORG_FULL           = 0x06,
	BAR_COLOR_GRN_FULL           = 0x07,
	LIGHTBAR_COLOR_TOTAL
};

struct lightbar_descriptor {
	enum ec_lightbar_colors color;
	uint8_t ticks;
};

#define BAR_INFINITE      UINT8_MAX
#define LIGHTBAR_ONE_SEC  (1000 / HOOK_TICK_INTERVAL_MS)
const struct lightbar_descriptor
	lb_table[LB_NUM_STATES][LIGHTBAR_NUM_PHASES] = {
	[LB_STATE_OFF]         = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_LID_CLOSE]   = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_AC_ONLY]     = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_AC_BAT_LOW]  = {{BAR_COLOR_ORG_20_PERCENT, BAR_INFINITE} },
	[LB_STATE_AC_BAT_20]   = {{BAR_COLOR_ORG_40_PERCENT, BAR_INFINITE} },
	[LB_STATE_AC_BAT_40]   = {{BAR_COLOR_ORG_60_PERCENT, BAR_INFINITE} },
	[LB_STATE_AC_BAT_60]   = {{BAR_COLOR_ORG_80_PERCENT, BAR_INFINITE} },
	[LB_STATE_AC_BAT_80]   = {{BAR_COLOR_ORG_FULL, BAR_INFINITE} },
	[LB_STATE_AC_BAT_100]  = {{BAR_COLOR_GRN_FULL, BAR_INFINITE} },
	[LB_STATE_BAT_LOW]     = {{BAR_OFF, 5*LIGHTBAR_ONE_SEC},
				{BAR_COLOR_ORG_FULL, LIGHTBAR_ONE_SEC} },
	[LB_STATE_BAT_ONLY]    = {{BAR_OFF, BAR_INFINITE} },
};

/*
 * From EE's information, lindar only support two colors lightbar,
 * Orange (Amber) and Green. And they connect KTD20xx's red color
 * channel to orange color led, and green color
 * channel to green color led.
 * Blue color channel is unused.
 */
#define DISABLE_LIGHTBAR	0x00
#define ENABLE_LIGHTBAR		0x80
#define I_OFF				0x00
#define I_ON				0x02
#define SEL_OFF				0x00
#define SEL_1ST_LED			BIT(7)
#define SEL_2ND_LED			BIT(3)
#define SEL_BOTH			(SEL_1ST_LED | SEL_2ND_LED)
#define SKU_ID_NONE			0x00
#define SKU_ID_INVALID		0x01
#define LB_SUPPORTED_SKUID_LOWER 458700
#define LB_SUPPORTED_SKUID_UPPER 458800

static bool lightbar_is_supported(void)
{
	static uint32_t skuid = SKU_ID_NONE;
	bool result;

	/*
	 * TODO(b/183826778):
	 * [Lillipup/Lindar] Move to SSFC/FW_CONFIG for lightbar supporting
	 * check
	 */
	if (skuid == SKU_ID_NONE) {
		if (cbi_get_sku_id(&skuid)) {
			CPRINTS("Cannot get skuid for lightbar supported");
			skuid = SKU_ID_INVALID;
		}
	}
	if (skuid >= LB_SUPPORTED_SKUID_LOWER &&
		skuid <= LB_SUPPORTED_SKUID_UPPER)
		result = true;
	else
		result = false;
	return result;
}

/*
 * Todo.
 * Maybe, we need to provide some command to tool kit to test lightbar
 * in factory. So, it may need a way to stop lightbar_update().
 */
static bool lightbar_is_enabled(void)
{
	if (!lightbar_is_supported())
		return false;

	/*
	 * Lightbar's I2C is powered by PP3300_A, and its power will be turn
	 * when system enter S4/S5. It may get I2C error if EC keep polling
	 * lightbar. We should stop it when EC doesn't turn on PP330_A.
	 */
	if (!board_is_i2c_port_powered(I2C_PORT_LIGHTBAR))
		return false;

	return true;
}

const uint8_t lightbar_ctrl[LIGHTBAR_COLOR_TOTAL][KTD20XX_TOTOAL_REG] = {
	[BAR_RESET] = {
		0x00, 0x00, DISABLE_LIGHTBAR,
		I_OFF, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_OFF] = {
		0x00, 0x00, DISABLE_LIGHTBAR,
		I_OFF, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_ORG_20_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_ORG_40_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_ORG_60_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_ORG_80_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_ORG_FULL] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF
	},
	[BAR_COLOR_GRN_FULL] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_OFF, I_ON, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF
	}
};

static void lightbar_set_color(enum ec_lightbar_colors color)
{
	enum ktd20xx_register i;

	if (color >= LIGHTBAR_COLOR_TOTAL) {
		CPRINTS("Lightbar Error! Incorrect lightbard color %d", color);
		color = BAR_RESET;
	}

	i2c_lock(I2C_PORT_LIGHTBAR, 1);
	for (i = KTD20XX_IRED_SET0; i <= KTD20XX_ISEL_C34; i++)
		controller_write(i, lightbar_ctrl[color][i]);

	controller_write(KTD20XX_CTRL_CFG,
					lightbar_ctrl[color][KTD20XX_CTRL_CFG]);

	i2c_lock(I2C_PORT_LIGHTBAR, 0);
}

static void lightbar_init(void)
{
	if (!lightbar_is_enabled())
		return;

	lightbar_set_color(BAR_RESET);
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, lightbar_init, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, lightbar_init, HOOK_PRIO_DEFAULT);

const int lightbar_bat_low = 15;
static enum lightbar_states lightbar_get_state(void)
{
	enum lightbar_states new_state = LB_NUM_STATES;
	int cur_bat_percent;

	cur_bat_percent = charge_get_percent();

	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		return LB_STATE_OFF;

	if (!lid_is_open())
		return LB_STATE_LID_CLOSE;

	if (extpower_is_present()) {
		if (charge_get_display_charge()) {
			if (cur_bat_percent < 20)
				new_state = LB_STATE_AC_BAT_LOW;
			else if (cur_bat_percent < 40)
				new_state = LB_STATE_AC_BAT_20;
			else if (cur_bat_percent < 60)
				new_state = LB_STATE_AC_BAT_40;
			else if (cur_bat_percent < 80)
				new_state = LB_STATE_AC_BAT_60;
			else if (cur_bat_percent < 97)
				new_state = LB_STATE_AC_BAT_80;
			else
				new_state = LB_STATE_AC_BAT_100;
		} else
			new_state = LB_STATE_AC_ONLY;
	} else {
		if (cur_bat_percent < lightbar_bat_low)
			new_state = LB_STATE_BAT_LOW;
		else
			new_state = LB_STATE_BAT_ONLY;
	}

	return new_state;
}

#define LIGHTBAR_DEBOUNCE_TICKS 1
static void lightbar_update(void)
{
	static uint8_t ticks, period;
	static enum lightbar_states lb_cur_state = LB_NUM_STATES;
	static int debounce_lightbar_state_update;
	enum lightbar_states desired_state;
	int phase;

	if (!lightbar_is_enabled())
		return;

	desired_state = lightbar_get_state();
	if (desired_state != lb_cur_state &&
		desired_state < LB_NUM_STATES) {
		/* State is changing */
		lb_cur_state = desired_state;
		/* Reset ticks and period when state changes */
		ticks = 0;

		period = lb_table[lb_cur_state][LIGHTBAR_PHASE_0].ticks +
			lb_table[lb_cur_state][LIGHTBAR_PHASE_1].ticks;

		/*
		 * System will be waken up when AC status change in S0ix. Due to
		 * EC may be late to update chipset state and cause lightbar
		 * flash a while when system transfer to S0. We add to debounce
		 * for any lightbar status change.
		 * It can make sure lightbar state is ready to to update.
		 */
		debounce_lightbar_state_update = LIGHTBAR_DEBOUNCE_TICKS;
	}

	/* If this state is undefined, turn lightbar off */
	if (period == 0) {
		CPRINTS("Undefined lightbar behavior for lightbar state %d,"
			"turning off lightbar", lb_cur_state);
		lightbar_set_color(BAR_OFF);
		return;
	}

	if (debounce_lightbar_state_update != 0) {
		debounce_lightbar_state_update--;
		return;
	}

	/*
	 * Determine which phase of the state table to use. The phase is
	 * determined if it falls within first phase time duration.
	 */
	phase = ticks < lb_table[lb_cur_state][LIGHTBAR_PHASE_0].ticks ? 0 : 1;
	ticks = (ticks + 1) % period;

	/* Set the color for the given state and phase */
	lightbar_set_color(lb_table[lb_cur_state][phase].color);

}

DECLARE_HOOK(HOOK_TICK, lightbar_update, HOOK_PRIO_DEFAULT);
