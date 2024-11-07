/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "i2c.h"
#include "cypress_pd_common.h"

/* 7 bit address  */
/* TODO: create a i2c ccg yaml to define the i2c address */
#define CCG_I2C_CHIP0	0x08

struct pd_chip_config_t pd_chip_config[] = {
	[PD_CHIP_0] = {
		.i2c_port = I2C_PORT_PD_MCU0,
		.addr_flags = CCG_I2C_CHIP0 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CCG_STATE_NO_POWER,
		.gpio = GPIO_EC_PD_INTA_L,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pd_chip_config) == CONFIG_PLATFORM_EC_PD_CHIP_MAX_COUNT);

struct pd_port_current_state_t pd_port_states[] = {
	[PD_PORT_0] = {

	},
	[PD_PORT_1] = {

	},
};
BUILD_ASSERT(ARRAY_SIZE(pd_port_states) == CONFIG_USB_PD_PORT_MAX_COUNT);

struct pd_chip_ucsi_info_t pd_chip_ucsi_info[] = {
	[PD_CHIP_0] = {

	},
};
BUILD_ASSERT(ARRAY_SIZE(pd_chip_ucsi_info) == CONFIG_PLATFORM_EC_PD_CHIP_MAX_COUNT);

void cypd_ccd_mode_control(void)
{
	if (pd_port_states[CONFIG_PD_CCG6_EC_UART_DEBUG_PORT].c_state == CCG_STATUS_DEBUG)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_sbu_uart_flip), 1);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_sbu_uart_flip), 0);

	if (pd_port_states[CONFIG_PD_CCG6_SOC_UART_DEBUG_PORT].c_state == CCG_STATUS_DEBUG)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_sbu_uart_flip_2), 1);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_sbu_uart_flip_2), 0);
}
