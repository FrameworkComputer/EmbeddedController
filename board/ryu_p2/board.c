/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ryu board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

void vbus_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD);
}

void unhandled_evt(enum gpio_signal signal)
{
	ccprintf("Unhandled INT %d,%d!\n", signal, gpio_get_level(signal));
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Determine recovery mode is requested by the power, volup, and
	 * voldown buttons being pressed.
	 */
	if (power_button_signal_asserted() &&
	    !gpio_get_level(GPIO_BTN_VOLD_L) &&
	    !gpio_get_level(GPIO_BTN_VOLU_L))
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_DEVICE_ODL lines are
	 * set low to specify device mode.
	 */
	gpio_set_level(GPIO_USBC_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_CHGR_ACOK);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_HOLD, 1, "AP_HOLD"},
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, /10 voltage divider. */
	[ADC_VBUS] = {"VBUS",  30000, 4096, 0, STM32_AIN(0)},
	/* USB PD CC lines sensing. Converted to mV (3000mV/4096). */
	[ADC_CC1_PD] = {"CC1_PD", 3000, 4096, 0, STM32_AIN(1)},
	[ADC_CC2_PD] = {"CC2_PD", 3000, 4096, 0, STM32_AIN(3)},
	/* Charger current sensing. Converted to mA. */
	[ADC_IADP] = {"IADP",  7500, 4096, 0, STM32_AIN(8)},
	[ADC_IBAT] = {"IBAT", 37500, 4096, 0, STM32_AIN(13)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void board_set_usb_mux(int port, enum typec_mux mux, int polarity)
{
	/* reset everything */
	gpio_set_level(GPIO_USBC_SS_EN_L, 1);
	gpio_set_level(GPIO_USBC_DP_MODE_L, 1);
	gpio_set_level(GPIO_USBC_DP_POLARITY, 1);
	gpio_set_level(GPIO_USBC_SS1_USB_MODE_L, 1);
	gpio_set_level(GPIO_USBC_SS2_USB_MODE_L, 1);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? GPIO_USBC_SS2_USB_MODE_L :
					  GPIO_USBC_SS1_USB_MODE_L, 0);
	}

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_DP_POLARITY, polarity);
		gpio_set_level(GPIO_USBC_DP_MODE_L, 0);
	}
	/* switch on superspeed lanes */
	gpio_set_level(GPIO_USBC_SS_EN_L, 0);
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_ss = !gpio_get_level(GPIO_USBC_SS_EN_L);
	int has_usb = !gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ||
		      !gpio_get_level(GPIO_USBC_SS2_USB_MODE_L);
	int has_dp = !gpio_get_level(GPIO_USBC_DP_MODE_L);

	if (has_dp)
		*dp_str = gpio_get_level(GPIO_USBC_DP_POLARITY) ? "DP2" : "DP1";
	else
		*dp_str = NULL;

	if (has_usb)
		*usb_str = gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ?
				"USB2" : "USB1";
	else
		*usb_str = NULL;

	return has_ss;
}

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

/* Battery temperature ranges in degrees C */
static const struct battery_info info = {
	/* Design voltage */
	.voltage_max    = 4350,
	.voltage_normal = 3800,
	.voltage_min    = 2800,
	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64,  /* mA */
	/* Operational temperature range */
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c       = 0,
	.charging_max_c       = 50,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}
