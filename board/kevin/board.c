/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/charger/bd99955.h"
#include "driver/tcpm/fusb302.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "shi_chip.h"
#include "switch.h"
#include "timer.h"
#include "thermal.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = {
		"BOARD_ID", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PP900_AP] = {
		"PP900_AP", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PP1200_LPDDR] = {
		"PP1200_LPDDR", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PPVAR_CLOGIC] = {
		"PPVAR_CLOGIC",
		NPCX_ADC_CH3, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PPVAR_LOGIC] = {
		"PPVAR_LOGIC", NPCX_ADC_CH4, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_BKLIGHT] = { 2, 0, 10000 },
	[PWM_CH_LED] =     { 3, PWM_CONFIG_DSLEEP, 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"tcpc0",   NPCX_I2C_PORT0_0, 1000, GPIO_I2C0_SCL0, GPIO_I2C0_SDA0},
	{"tcpc1",   NPCX_I2C_PORT0_1, 1000, GPIO_I2C0_SCL1, GPIO_I2C0_SDA1},
	{"sensors", NPCX_I2C_PORT1,   1000, GPIO_I2C1_SCL,  GPIO_I2C1_SDA},
	{"charger", NPCX_I2C_PORT2,    400, GPIO_I2C2_SCL,  GPIO_I2C2_SDA},
	{"battery", NPCX_I2C_PORT3,    100, GPIO_I2C3_SCL,  GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 40,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xf6, 0x55, 0xfa, 0xc8  /* full set */
	},
};

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_VOLUME_DOWN_L,
	 30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_VOLUME_UP_L,
	 30 * MSEC, 0},
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{I2C_PORT_TCPC0, FUSB302_I2C_SLAVE_ADDR, &fusb302_tcpm_drv},
	{I2C_PORT_TCPC1, FUSB302_I2C_SLAVE_ADDR, &fusb302_tcpm_drv},
};

void board_reset_pd_mcu(void)
{
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

int board_set_active_charge_port(int charge_port)
{
	enum bd99955_charge_port bd99955_port;

	CPRINTS("New chg p%d", charge_port);

	switch (charge_port) {
	case 0:
		bd99955_port = BD99955_CHARGE_PORT_VBUS;
		break;
	case 1:
		bd99955_port = BD99955_CHARGE_PORT_VCC;
		break;
	case CHARGE_PORT_NONE:
		bd99955_port = BD99955_CHARGE_PORT_NONE;
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	return bd99955_select_input_port(bd99955_port);
}

void board_set_charge_limit(int charge_ma)
{
	charge_set_input_current_limit(MAX(charge_ma,
				       CONFIG_CHARGER_INPUT_CURRENT));
}

int extpower_is_present(void)
{
	return bd99955_extpower_is_present();
}

static void board_init(void)
{
	struct charge_port_info charge_none;
	int i;

	/* Initialize all pericom charge suppliers to 0 */
	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
	/* TODO: Implement BC1.2 + VBUS detection */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_OTHER,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS,
					     i,
					     &charge_none);
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
