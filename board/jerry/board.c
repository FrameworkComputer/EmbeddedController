/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Veyron board-specific configuration */

#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "common.h"
#include "driver/charger/bq24715.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"
#include "timer.h"
#include "charger.h"
#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_SOC_POWER_GOOD, POWER_SIGNAL_ACTIVE_HIGH, "POWER_GOOD"},
	{GPIO_SUSPEND_L,      POWER_SIGNAL_ACTIVE_HIGH, "SUSPEND#_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(2), STM32_TIM_CH(3),
	 PWM_CONFIG_ACTIVE_LOW},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Charger chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = CHARGER_ADDR_FLAGS,
		.drv = &bq24715_drv,
	},
};

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);

	/* Remap USART DMA to match the USART driver */
	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1
	 *  Chan 3 : SPI1_TX
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */
	STM32_SYSCFG_CFGR1 |= BIT(9) | BIT(10); /* Remap USART1 RX/TX DMA */
}
