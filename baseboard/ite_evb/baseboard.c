/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ite_evb baseboard configuration */
#include "clock.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "lpc.h"
#include "power_button.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "gpio_list.h"

#if defined(CONFIG_FANS) || defined(CONFIG_PWM)
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1500,
	.rpm_start = 1500,
	.rpm_max = 6500,
};

const struct fan_t fans[] = {
	{ .conf = &fan_conf_0,
	  .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/*
 * PWM HW channelx binding tachometer channelx for fan control.
 * Four tachometer input pins but two tachometer modules only,
 * so always binding [TACH_CH_TACH0A | TACH_CH_TACH0B] and/or
 * [TACH_CH_TACH1A | TACH_CH_TACH1B]
 */
const struct fan_tach_t fan_tach[] = {
	[PWM_HW_CH_DCR0] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR1] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR2] = {
		.ch_tach = TACH_CH_TACH1A,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR3] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR4] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR5] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR6] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR7] = {
		.ch_tach = TACH_CH_TACH0A,
		.fan_p = 2,
		.rpm_re = 50,
		.s_duty = 30,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fan_tach) == PWM_HW_CH_TOTAL);
#endif /* defined(CONFIG_FANS) || defined(CONFIG_PWM) */

#if defined(CONFIG_KEYBOARD_BOARD_CONFIG)
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
#endif

#if defined(CONFIG_SPI_FLASH_PORT)
/* SPI devices */
const struct spi_device_t spi_devices[] = {
	[CONFIG_SPI_FLASH_PORT] = {
		.port = CONFIG_SPI_FLASH_PORT,
		.div = 0,
		.gpio_cs = -1
	},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
#endif

/* Initialize board. */
static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L, GPIO_LID_OPEN
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/*
 * I2C channels (A, B, and C) are using the same timing registers (00h~07h)
 * at default.
 * In order to set frequency independently for each channels,
 * We use timing registers 09h~0Bh, and the supported frequency will be:
 * 50KHz, 100KHz, 400KHz, or 1MHz.
 * I2C channels (D, E and F) can be set different frequency on different ports.
 * The I2C(D/E/F) frequency depend on the frequency of SMBus Module and
 * the individual prescale register.
 * The frequency of SMBus module is 24MHz on default.
 * The allowed range of I2C(D/E/F) frequency is as following setting.
 * SMBus Module Freq = PLL_CLOCK / ((IT83XX_ECPM_SCDCR2 & 0x0F) + 1)
 * (SMBus Module Freq / 510) <=  I2C Freq <= (SMBus Module Freq / 8)
 * Channel D has multi-function and can be used as UART interface.
 * Channel F is reserved for EC debug.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl = GPIO_I2C_C_SCL,
		.sda = GPIO_I2C_C_SDA,
	},
	{
		.name = "evb-1",
		.port = IT83XX_I2C_CH_A,
		.kbps = 100,
		.scl = GPIO_I2C_A_SCL,
		.sda = GPIO_I2C_A_SDA,
	},
	{
		.name = "evb-2",
		.port = IT83XX_I2C_CH_B,
		.kbps = 100,
		.scl = GPIO_I2C_B_SCL,
		.sda = GPIO_I2C_B_SDA,
	},
	{
		.name = "opt-4",
		.port = IT83XX_I2C_CH_E,
		.kbps = 100,
		.scl = GPIO_I2C_E_SCL,
		.sda = GPIO_I2C_E_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
