/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Plankton board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

/* Debounce time for voltage buttons */
#define BUTTON_DEBOUNCE_US (100 * MSEC)

static enum gpio_signal button_pressed;

enum usbc_action {
	USBC_ACT_5V_TO_DUT,
	USBC_ACT_12V_TO_DUT,
	USBC_ACT_DEVICE,
	USBC_ACT_USB_EN,
	USBC_ACT_DP_EN,
	USBC_ACT_CABLE_FLIP,
	USBC_ACT_CABLE_POLARITY0,
	USBC_ACT_CABLE_POLARITY1,
};

static void set_usbc_action(enum usbc_action act)
{
	switch (act) {
	case USBC_ACT_5V_TO_DUT: /* TODO: Force 5V */
	case USBC_ACT_12V_TO_DUT:
		pd_set_dual_role(PD_DRP_FORCE_SOURCE);
		break;
	case USBC_ACT_DEVICE:
		pd_set_dual_role(PD_DRP_FORCE_SINK);
		break;
	case USBC_ACT_USB_EN:
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 1);
		break;
	case USBC_ACT_DP_EN:
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 0);
		break;
	case USBC_ACT_CABLE_FLIP:
		gpio_set_level(GPIO_USBC_DP_POLARITY,
				!gpio_get_level(GPIO_USBC_DP_POLARITY));
		break;
	case USBC_ACT_CABLE_POLARITY0:
		gpio_set_level(GPIO_USBC_DP_POLARITY, 0);
		break;
	case USBC_ACT_CABLE_POLARITY1:
		gpio_set_level(GPIO_USBC_DP_POLARITY, 1);
		break;
	default:
		break;
	}
}

/* Handle debounced button press */
static void button_deferred(void)
{
	/* don't do anything if not in debug mode */
	if (!gpio_get_level(GPIO_DBG_MODE_EN))
		return;

	/* bounce ? */
	if (gpio_get_level(button_pressed) != 0)
		return;

	switch (button_pressed) {
	case GPIO_DBG_5V_TO_DUT_L:
		set_usbc_action(USBC_ACT_5V_TO_DUT);
		break;
	case GPIO_DBG_12V_TO_DUT_L:
		set_usbc_action(USBC_ACT_12V_TO_DUT);
		break;
	case GPIO_DBG_CHG_TO_DEV_L:
		set_usbc_action(USBC_ACT_DEVICE);
		break;
	case GPIO_DBG_USB_EN_L:
		set_usbc_action(USBC_ACT_USB_EN);
		break;
	case GPIO_DBG_DP_EN_L:
		set_usbc_action(USBC_ACT_DP_EN);
		break;
	case GPIO_DBG_CABLE_FLIP_L:
		set_usbc_action(USBC_ACT_CABLE_FLIP);
		break;
	default:
		break;
	}

	ccprintf("Button %d = %d\n",
		 button_pressed, gpio_get_level(button_pressed));
}
DECLARE_DEFERRED(button_deferred);

void button_event(enum gpio_signal signal)
{
	button_pressed = signal;
	/* reset debounce time */
	hook_call_deferred(button_deferred, BUTTON_DEBOUNCE_US);
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS! =%d\n", gpio_get_level(signal));
	task_wake(TASK_ID_PD);
}

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(1)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void board_init(void)
{
	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_VBUS_WAKE);

	/* Enable button interrupts. */
	gpio_enable_interrupt(GPIO_DBG_12V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_CHG_TO_DEV_L);
	gpio_enable_interrupt(GPIO_DBG_5V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_USB_EN_L);
	gpio_enable_interrupt(GPIO_DBG_DP_EN_L);
	gpio_enable_interrupt(GPIO_DBG_STATUS_CLEAR_L);
	gpio_enable_interrupt(GPIO_DBG_CABLE_FLIP_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static int cmd_usbc_action(int argc, char *argv[])
{
	enum usbc_action act;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "5v"))
		act = USBC_ACT_5V_TO_DUT;
	else if (!strcasecmp(argv[1], "12v"))
		act = USBC_ACT_12V_TO_DUT;
	else if (!strcasecmp(argv[1], "dev"))
		act = USBC_ACT_DEVICE;
	else if (!strcasecmp(argv[1], "usb"))
		act = USBC_ACT_USB_EN;
	else if (!strcasecmp(argv[1], "dp"))
		act = USBC_ACT_DP_EN;
	else if (!strcasecmp(argv[1], "flip"))
		act = USBC_ACT_CABLE_FLIP;
	else if (!strcasecmp(argv[1], "pol0"))
		act = USBC_ACT_CABLE_POLARITY0;
	else if (!strcasecmp(argv[1], "pol1"))
		act = USBC_ACT_CABLE_POLARITY1;
	else
		return EC_ERROR_PARAM1;

	set_usbc_action(act);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(usbc_action, cmd_usbc_action,
			"<5v | 12v | dev | usb | dp | flip | pol0 | pol1>",
			"Set Plankton type-C port state",
			NULL);
