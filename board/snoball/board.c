/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Snoball board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "fusb302.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

void tcpc_alert_event(enum gpio_signal signal)
{
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
}

#include "gpio_list.h"

const struct i2c_port_t i2c_ports[] = {
	{"tcpc-a", STM32_I2C1_PORT, 1000, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc-b", STM32_I2C2_PORT, 1000, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{STM32_I2C1_PORT, FUSB302_I2C_SLAVE_ADDR},
	{STM32_I2C2_PORT, FUSB302_I2C_SLAVE_ADDR},
	/* TODO: Verify secondary slave addr, or use i2c mux */
	{STM32_I2C2_PORT, FUSB302_I2C_SLAVE_ADDR + 2},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_TCPC1_INT))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_TCPC2_INT))
		status |= PD_STATUS_TCPC_ALERT_1;
	if (!gpio_get_level(GPIO_TCPC3_INT))
		status |= PD_STATUS_TCPC_ALERT_2;

	return status;
}

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Current sensing. Converted to mA (6600mV/4096). */
	[ADC_C0_CS] = {"C0_CS", 6600, 4096, 0, STM32_AIN(0)},
	[ADC_C1_CS] = {"C1_CS", 6600, 4096, 0, STM32_AIN(1)},
	[ADC_C2_CS] = {"C2_CS", 6600, 4096, 0, STM32_AIN(2)},
	/* Voltage sensing. Converted to mV (40000mV/4096). */
	[ADC_C0_VS] = {"C0_VS", 40000, 4096, 0, STM32_AIN(3)},
	[ADC_C1_VS] = {"C1_VS", 40000, 4096, 0, STM32_AIN(4)},
	[ADC_C2_VS] = {"C2_VS", 40000, 4096, 0, STM32_AIN(5)},
	[ADC_VBUCK] = {"VBUCK",  40000, 4096, 0, STM32_AIN(8)},
	/* TODO: Check characteristics of thermistor circuit */
	[ADC_TEMP] = {"TEMP", 3300, 4096, 0, STM32_AIN(9)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

static void board_init(void)
{
	gpio_enable_interrupt(GPIO_TCPC1_INT);
	gpio_enable_interrupt(GPIO_TCPC2_INT);
	gpio_enable_interrupt(GPIO_TCPC3_INT);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_reset_pd_mcu(void)
{
}
