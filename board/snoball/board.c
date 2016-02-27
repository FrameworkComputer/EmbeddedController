/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Snoball board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "util.h"

void tcpc_alert_event(enum gpio_signal signal)
{
	/* Exchange status with TCPCs */
	ccprintf("TCPC alert!\n");
}

#include "gpio_list.h"

const struct i2c_port_t i2c_ports[] = {
	{"tcpc-a", STM32_I2C1_PORT, 400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc-b", STM32_I2C2_PORT, 400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* VBIAS input voltage, through /2 divider. */
	[ADC_VBIAS] = {"VBIAS", 6600, 4096, 0, STM32_AIN(4)},
	[ADC_VOUT_1] = {"VOUT_1", 39600, 4096, 0, STM32_AIN(5)},
	[ADC_VOUT_2] = {"VOUT_2", 39600, 4096, 0, STM32_AIN(6)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);


static void board_init(void)
{
	gpio_enable_interrupt(GPIO_TCPC1_INT);
	gpio_enable_interrupt(GPIO_TCPC2_INT);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
	/*
	 * the DMA mapping is :
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */

	/* Remap USART1 RX/TX DMA to match uart driver. */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10);
}
