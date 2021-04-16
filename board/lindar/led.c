/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Malefor
 */

#include "cbi_ssfc.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "ktd20xx.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "lid_switch.h"
#include "stdbool.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define LED_OFF_LVL	1
#define LED_ON_LVL	0

__override const int led_charge_lvl_1 = 5;

__override const int led_charge_lvl_2 = 97;

__override struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_RED, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_GREEN, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_DISCHARGE_S3]	     = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_RED,   1 * LED_ONE_SEC},
					{LED_OFF,            1 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_RED,   2 * LED_ONE_SEC},
					{EC_LED_COLOR_GREEN, 2 * LED_ONE_SEC} },
};

__override const struct led_descriptor
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

__override void led_set_color_power(enum ec_led_colors color)
{
	if (color == EC_LED_COLOR_WHITE)
		gpio_set_level(GPIO_LED_3_L, LED_ON_LVL);
	else
		/* LED_OFF and unsupported colors */
		gpio_set_level(GPIO_LED_3_L, LED_OFF_LVL);
}

__override void led_set_color_battery(enum ec_led_colors color)
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

static const uint16_t ktd2061_i2c_addr = 0x68;
static void controller_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2];

	buf[0] = reg;
	buf[1] = val;

	i2c_xfer_unlocked(I2C_PORT_LIGHTBAR, ktd2061_i2c_addr,
			buf, 2, 0, 0,
			I2C_XFER_SINGLE);
}

enum lightbar_states {
	LB_STATE_OFF,
	LB_STATE_LID_CLOSE,
	LB_STATE_SLEEP_AC_ONLY,
	LB_STATE_SLEEP_AC_BAT_LOW,
	LB_STATE_SLEEP_AC_BAT_LV1,
	LB_STATE_SLEEP_AC_BAT_LV2,
	LB_STATE_SLEEP_AC_BAT_LV3,
	LB_STATE_SLEEP_AC_BAT_LV4,
	LB_STATE_SLEEP_BAT_LOW,
	LB_STATE_SLEEP_BAT_ONLY,
	LB_STATE_S0_AC_ONLY,
	LB_STATE_S0_BAT_LOW,
	LB_STATE_S0_BAT_LV1,
	LB_STATE_S0_BAT_LV2,
	LB_STATE_S0_BAT_LV3,
	LB_STATE_S0_BAT_LV4,
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
	BAR_RESET,
	BAR_OFF,
	BAR_COLOR_ORG_20_PERCENT,
	BAR_COLOR_GRN_40_PERCENT,
	BAR_COLOR_GRN_60_PERCENT,
	BAR_COLOR_GRN_80_PERCENT,
	BAR_COLOR_GRN_FULL,
	BAR_COLOR_ORG_FULL,
	LIGHTBAR_COLOR_TOTAL
};

struct lightbar_descriptor {
	enum ec_lightbar_colors color;
	uint8_t ticks;
};

#define BAR_INFINITE      UINT8_MAX
#define LIGHTBAR_ONE_SEC  (1000 / HOOK_TICK_INTERVAL_MS)
#define LIGHTBAR_COUNT_FOR_RESUME_FROM_SLEEP (3 * LIGHTBAR_ONE_SEC)
int lightbar_resume_tick;

const struct lightbar_descriptor
	lb_table[LB_NUM_STATES][LIGHTBAR_NUM_PHASES] = {
	[LB_STATE_OFF]              = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_LID_CLOSE]        = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_SLEEP_AC_ONLY]    = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_SLEEP_AC_BAT_LOW] = {{BAR_COLOR_ORG_20_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_SLEEP_AC_BAT_LV1] = {{BAR_COLOR_GRN_40_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_SLEEP_AC_BAT_LV2] = {{BAR_COLOR_GRN_60_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_SLEEP_AC_BAT_LV3] = {{BAR_COLOR_GRN_80_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_SLEEP_AC_BAT_LV4] = {{BAR_COLOR_GRN_FULL, BAR_INFINITE} },
	[LB_STATE_SLEEP_BAT_LOW]    = {{BAR_OFF, 5 * LIGHTBAR_ONE_SEC},
				{BAR_COLOR_ORG_FULL, LIGHTBAR_ONE_SEC} },
	[LB_STATE_SLEEP_BAT_ONLY]   = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_S0_AC_ONLY]       = {{BAR_OFF, BAR_INFINITE} },
	[LB_STATE_S0_BAT_LOW]       = {{BAR_COLOR_ORG_20_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_S0_BAT_LV1]       = {{BAR_COLOR_GRN_40_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_S0_BAT_LV2]       = {{BAR_COLOR_GRN_60_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_S0_BAT_LV3]       = {{BAR_COLOR_GRN_80_PERCENT,
				BAR_INFINITE} },
	[LB_STATE_S0_BAT_LV4]       = {{BAR_COLOR_GRN_FULL, BAR_INFINITE} },
};

#define DISABLE_LIGHTBAR         0x00
#define ENABLE_LIGHTBAR          0x80
#define I_OFF                    0x00
#define GRN_I_ON                 0x1E
#define ORG_I_ON                 0x28
#define SEL_OFF                  0x00
#define SEL_1ST_LED              BIT(7)
#define SEL_2ND_LED              BIT(3)
#define SEL_BOTH                 (SEL_1ST_LED | SEL_2ND_LED)
#define SKU_ID_NONE              0x00
#define SKU_ID_INVALID           0x01
#define LB_SUPPORTED_SKUID_LOWER 458700
#define LB_SUPPORTED_SKUID_UPPER 458800

static bool lightbar_is_supported(void)
{
	static uint32_t skuid = SKU_ID_NONE;
	bool result;

	/* lindar add SSFC tag to cbi image from "board_id = 3". */
	if (get_board_id() >= 3) {
		if (get_cbi_ssfc_lightbar() == SSFC_LIGHTBAR_NONE)
			return false;
		return true;
	}

	if (skuid == SKU_ID_NONE) {
		if (cbi_get_sku_id(&skuid)) {
			CPRINTS("Cannot get skuid for lightbar supported");
			skuid = SKU_ID_INVALID;
		}
	}

	/*
	 * If board_id = 1 or 2, it needs to check sku_id to know
	 * if system support lightbar or not.
	 */
	if (skuid >= LB_SUPPORTED_SKUID_LOWER &&
		skuid <= LB_SUPPORTED_SKUID_UPPER)
		result = true;
	else
		result = false;

	return result;
}

/*
 * lightbar_enter_s0ix_s3:
 * This flag is used to know if system ever enter S0ix/S3.
 * Lightbar V9 SPEC define lightbar resuming behavior, "S0ix/S3 -> S0",
 * but not include "G3/S5/S4 -> S0". "G3/S5/S4 -> S0" need to keep off.
 */
static bool lightbar_enter_s0ix_s3;

/*
 * lightbar_auto_control:
 * We need some command for testing lightbar in factory.
 * So, create this flag to stop regular action in lightbar_update().
 *
 * lightbar_demo_state:
 * It's used for testing lightbar via executing command under
 * console.
 */
static bool lightbar_auto_control;
static enum lightbar_states lightbar_demo_state;

static void lightbar_set_auto_control(bool state)
{
	lightbar_auto_control = state;
}

static bool lightbar_is_auto_control(void)
{
	return lightbar_auto_control;
}

static void lightbar_set_demo_state(enum lightbar_states tmp_state)
{
	if (tmp_state >= LB_NUM_STATES || tmp_state < LB_STATE_OFF) {
		lightbar_demo_state = LB_NUM_STATES;
		lightbar_resume_tick = 0;
	} else {
		lightbar_demo_state = tmp_state;

		if (lightbar_demo_state >= LB_STATE_S0_AC_ONLY)
			lightbar_resume_tick =
				LIGHTBAR_COUNT_FOR_RESUME_FROM_SLEEP;
	}
	ccprintf("lightbar_demo_state = %d; lightbar_resume_tick %d.\n",
			lightbar_demo_state,
			lightbar_resume_tick);
}

static enum lightbar_states lightbar_get_demo_state(void)
{
	/*
	 * Once tick count to zero, it needs to return LB_STATE_OFF to
	 * simulate lightbar off.
	 */
	if ((lightbar_demo_state != LB_NUM_STATES) &&
		(lightbar_demo_state >= LB_STATE_S0_AC_ONLY) &&
		(lightbar_resume_tick == 0))
		return LB_STATE_OFF;

	return lightbar_demo_state;
}

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

/*
 * From "board_id = 3", HW change lightbar circuit, and it only support
 * two colors, orange (amber) and green. It connects KTD20xx's red-channel
 * green color led, and green-channel to orange color led.
 * Blue-channel is unused.
 *
 * The configuration format of lightbar_xx_led_cfg's is as below.
 * ID_DAT, STATUS_REG, CTRL_CFG
 * IRED_SET0, IGRN_SET0, IBLU_SET0, IRED_SET1, IGRN_SET1, IBLU_SET1
 * ISEL_A12, ISEL_A34, ISEL_B12, ISEL_B34, ISEL_C12, ISEL_C34
 */
const uint8_t lightbar_10_led_cfg[LIGHTBAR_COLOR_TOTAL][KTD20XX_TOTOAL_REG] = {
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
		I_OFF, ORG_I_ON, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_OFF, SEL_BOTH, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_40_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_60_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_BOTH, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_80_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_FULL] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF
	},
	[BAR_COLOR_ORG_FULL] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_OFF, ORG_I_ON, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF
	}
};

const uint8_t lightbar_12_led_cfg[LIGHTBAR_COLOR_TOTAL][KTD20XX_TOTOAL_REG] = {
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
		I_OFF, ORG_I_ON, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_2ND_LED, SEL_BOTH, SEL_OFF, SEL_OFF, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_40_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_2ND_LED, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_60_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_2ND_LED, SEL_BOTH, SEL_OFF, SEL_OFF
	},
	[BAR_COLOR_GRN_80_PERCENT] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_OFF, SEL_2ND_LED
	},
	[BAR_COLOR_GRN_FULL] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		GRN_I_ON, I_OFF, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH
	},
	[BAR_COLOR_ORG_FULL] = {
		0x00, 0x00, ENABLE_LIGHTBAR,
		I_OFF, ORG_I_ON, I_OFF, I_OFF, I_OFF, I_OFF,
		SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH, SEL_BOTH
	}
};

/*
 * lightbar_ctrl is a pointer to 2-dimension lightbar configuration. It's used
 * to base on DUT type to load different cfg.
 * Default is lightbar_10_led_cfg.
 */
const uint8_t (*lightbar_ctrl)[KTD20XX_TOTOAL_REG] = lightbar_10_led_cfg;

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

	if (get_cbi_ssfc_lightbar() == SSFC_LIGHTBAR_12_LED)
		lightbar_ctrl = lightbar_12_led_cfg;
	else
		lightbar_ctrl = lightbar_10_led_cfg;

	/* Clear this flag if system doesn't enter S0ix/S3 */
	lightbar_enter_s0ix_s3 = false;
	lightbar_resume_tick = 0;

	lightbar_set_color(BAR_RESET);
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, lightbar_init, HOOK_PRIO_DEFAULT);

static void lightbar_sleep_entry(void)
{
	if (!lightbar_is_enabled())
		return;

	lightbar_set_auto_control(true);
	/*
	 * Set this flag, then EC'll base on it to set resume tick after
	 * S0ix/S3 exit.
	 */
	lightbar_enter_s0ix_s3 = true;
	lightbar_resume_tick = 0;

	lightbar_set_color(BAR_RESET);
}

DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, lightbar_sleep_entry, HOOK_PRIO_DEFAULT);

static void lightbar_sleep_exit(void)
{
	if (!lightbar_is_enabled())
		return;

	lightbar_set_auto_control(true);
	if (lightbar_enter_s0ix_s3)
		lightbar_resume_tick = LIGHTBAR_COUNT_FOR_RESUME_FROM_SLEEP;
	else
		lightbar_resume_tick = 0;
	lightbar_enter_s0ix_s3 = false;
}

DECLARE_HOOK(HOOK_CHIPSET_RESUME, lightbar_sleep_exit, HOOK_PRIO_DEFAULT);

#define LB_BAT_THRESHOLD_1	16
#define LB_BAT_THRESHOLD_2	40
#define LB_BAT_THRESHOLD_3	60
#define LB_BAT_THRESHOLD_4	80

static enum lightbar_states lightbar_get_state(void)
{
	enum lightbar_states new_state = LB_NUM_STATES;
	int cur_bat_percent;

	cur_bat_percent = charge_get_percent();

	if (!lid_is_open())
		return LB_STATE_LID_CLOSE;

	if (lightbar_resume_tick) {
		if ((battery_is_present() == BP_YES) &&
			charge_get_display_charge()) {
			if (cur_bat_percent < LB_BAT_THRESHOLD_1)
				new_state = LB_STATE_S0_BAT_LOW;
			else if (cur_bat_percent < LB_BAT_THRESHOLD_2)
				new_state = LB_STATE_S0_BAT_LV1;
			else if (cur_bat_percent < LB_BAT_THRESHOLD_3)
				new_state = LB_STATE_S0_BAT_LV2;
			else if (cur_bat_percent < LB_BAT_THRESHOLD_4)
				new_state = LB_STATE_S0_BAT_LV3;
			else
				new_state = LB_STATE_S0_BAT_LV4;
		} else
			new_state = LB_STATE_S0_AC_ONLY;
		return new_state;
	}

	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		return LB_STATE_OFF;

	if (extpower_is_present()) {
		if ((battery_is_present() == BP_YES) &&
			charge_get_display_charge()) {
			if (cur_bat_percent < LB_BAT_THRESHOLD_1)
				new_state = LB_STATE_SLEEP_AC_BAT_LOW;
			else if (cur_bat_percent < LB_BAT_THRESHOLD_2)
				new_state = LB_STATE_SLEEP_AC_BAT_LV1;
			else if (cur_bat_percent < LB_BAT_THRESHOLD_3)
				new_state = LB_STATE_SLEEP_AC_BAT_LV2;
			else if (cur_bat_percent < LB_BAT_THRESHOLD_4)
				new_state = LB_STATE_SLEEP_AC_BAT_LV3;
			else
				new_state = LB_STATE_SLEEP_AC_BAT_LV4;
		} else
			new_state = LB_STATE_SLEEP_AC_ONLY;
	} else {
		if (cur_bat_percent < LB_BAT_THRESHOLD_1)
			new_state = LB_STATE_SLEEP_BAT_LOW;
		else
			new_state = LB_STATE_SLEEP_BAT_ONLY;
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

	if (lightbar_is_auto_control())
		desired_state = lightbar_get_state();
	else {
		desired_state = lightbar_get_demo_state();
		/*
		 * Stop to update lb_cur_state if desired_state is equal to
		 * LB_NUM_STATES.
		 */
		if (desired_state == LB_NUM_STATES)
			return;
	}

	if (lightbar_resume_tick)
		lightbar_resume_tick--;

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

/****************************************************************************/
/* EC console commands for lightbar */
/****************************************************************************/
static void lightbar_dump_status(void)
{
	uint32_t cbi_bid, cbi_skuid;
	int cbi_ssfc_lightbar;

	ccprintf("lightbar is %ssupported, %sabled, auto_control: %sabled\n",
			lightbar_is_supported()?"":"un-",
			lightbar_is_enabled()?"en":"dis",
			lightbar_is_auto_control()?"en":"dis");

	cbi_bid = get_board_id();
	cbi_get_sku_id(&cbi_skuid);
	cbi_ssfc_lightbar = get_cbi_ssfc_lightbar();
	ccprintf("board id = %d, skuid = %d, ssfc_lightbar = %d\n",
			cbi_bid,
			cbi_skuid,
			cbi_ssfc_lightbar);
}

#ifdef CONFIG_CONSOLE_CMDHELP
static int help(const char *cmd)
{
	ccprintf("Usage:\n");
	ccprintf("  %s                       - dump lightbar status\n", cmd);
	ccprintf("  %s on                    - set on lightbar auto control\n",
				cmd);
	ccprintf("  %s off                   - set off lightbar auto control\n",
				cmd);
	ccprintf("  %s demo [%x - %x]          - demo lightbar state\n",
				cmd, LB_STATE_OFF, (LB_NUM_STATES - 1));
	return EC_SUCCESS;
}
#endif

static int command_lightbar(int argc, char **argv)
{
	/* no args = dump lightbar status */
	if (argc == 1) {
		lightbar_dump_status();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "help")) {
	#ifdef CONFIG_CONSOLE_CMDHELP
		help(argv[0]);
	#endif
		return EC_SUCCESS;
	}

	if (!lightbar_is_enabled()) {
		lightbar_dump_status();
		return EC_ERROR_UNIMPLEMENTED;
	}

	if (!strcasecmp(argv[1], "on")) {
		lightbar_set_auto_control(true);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "off")) {
		lightbar_set_auto_control(false);
		lightbar_set_demo_state(LB_NUM_STATES);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "demo")) {
		int lb_demo_state;
		char *e;

		/* Need to disable auto_control before demo */
		if (lightbar_is_auto_control()) {
			ccprintf("Please set off auto control before demo.\n");
			return EC_ERROR_ACCESS_DENIED;
		}

		lb_demo_state = 0xff & strtoi(argv[2], &e, 16);
		lightbar_set_demo_state(lb_demo_state);
		return EC_SUCCESS;
	}

#ifdef CONFIG_CONSOLE_CMDHELP
	help(argv[0]);
#endif

	return EC_ERROR_INVAL;
}

DECLARE_CONSOLE_COMMAND(lightbar, command_lightbar,
			"[help | on | off | demo]",
			"get/set lightbar status");

/****************************************************************************/
/* EC host commands (ectool) for lightbar */
/****************************************************************************/
static enum ec_status lpc_cmd_lightbar(struct host_cmd_handler_args *args)
{
	const struct ec_params_lightbar *in = args->params;
	int lb_demo_state;

	/*
	 * HOST_CMD is binded with ectool. From ectool.c, it already define
	 * command format.
	 * We only base on "off", "on", and "seq" to do what we can do
	 * now.
	 * Originally, I expect to use "demo", but it limit "in->demo.num"
	 * within 0~1. So, adopt "seq" command for basic testing.
	 */
	switch (in->cmd) {
	case LIGHTBAR_CMD_OFF:
		lightbar_set_auto_control(false);
		lightbar_set_demo_state(LB_NUM_STATES);
		break;
	case LIGHTBAR_CMD_ON:
		lightbar_set_auto_control(true);
		break;
	case LIGHTBAR_CMD_SEQ:
		lb_demo_state = in->seq.num;
		if (lightbar_is_auto_control()) {
			CPRINTS("Please set off auto control before demo.");
			return EC_RES_ACCESS_DENIED;
		}
		lightbar_set_demo_state(lb_demo_state);
		break;
	default:
		CPRINTS("LB bad cmd 0x%x", in->cmd);
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_LIGHTBAR_CMD,
		     lpc_cmd_lightbar,
		     EC_VER_MASK(0));
