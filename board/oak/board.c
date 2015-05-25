/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Oak board configuration */

#include "adc_chip.h"
#include "battery.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "pi3usb30532.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "task.h"
#include "timer.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

static void ap_reset_deferred(void)
{
	/* Warm reset AP */
	chipset_reset(0);
}
DECLARE_DEFERRED(ap_reset_deferred);

void ap_reset_interrupt(enum gpio_signal signal)
{
	if (gpio_get_level(GPIO_AP_RESET_L) == 0)
		hook_call_deferred(ap_reset_deferred, 0);
}

void vbus_wake_interrupt(enum gpio_signal signal)
{
	CPRINTF("VBUS %d\n", !gpio_get_level(signal));
	gpio_set_level(GPIO_USB_PD_VBUS_WAKE,
		       !gpio_get_level(GPIO_VBUS_WAKE_L));
	task_wake(TASK_ID_PD);
}

void pd_mcu_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(tcpc_alert, 0);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_SOC_POWER_GOOD, 1, "POWER_GOOD"},	/* Active high */
	{GPIO_SUSPEND_L, 0, "SUSPEND#_ASSERTED"},	/* Active low */
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* VDC_BOOSTIN_SENSE(PC1): ADC_IN11, output in mV */
	[ADC_VBUS] = {"VBUS", 33000, 4096, 0, STM32_AIN(11)},
	/*
	 * PSYS_MONITOR(PA2): ADC_IN2, 1.44 uA/W on 6.05k Ohm
	 * output in mW
	 */
	[ADC_PSYS] = {"PSYS", 379415, 4096, 0, STM32_AIN(2)},
	/* AMON_BMON(PC0): ADC_IN10, output in uV */
	[ADC_AMON_BMON] = {"AMON_BMON", 183333, 4096, 0, STM32_AIN(10)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"battery", I2C_PORT_BATTERY, 100,  GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"pd",      I2C_PORT_PD_MCU,  1000, GPIO_I2C1_SCL, GPIO_I2C1_SDA}
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static int discharging_on_ac;

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	int rv = charger_discharge_on_ac(enable);

	if (rv == EC_SUCCESS)
		discharging_on_ac = enable;

	return rv;
}

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	usleep(100);
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
}

void __board_i2c_set_timeout(int port, uint32_t timeout)
{
}

void i2c_set_timeout(int port, uint32_t timeout)
		__attribute__((weak, alias("__board_i2c_set_timeout")));

/* Initialize board. */
static void board_init(void)
{
	/* Enable rev1 testing GPIOs */
	gpio_set_level(GPIO_SYSTEM_POWER_H, 1);
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_VBUS_WAKE_L);
#ifdef CONFIG_AP_WARM_RESET_INTERRUPT
	gpio_enable_interrupt(GPIO_AP_RESET_L);
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#ifndef CONFIG_AP_WARM_RESET_INTERRUPT
/* Using this hook if system doesn't have enough external line. */
static void check_ap_reset_second(void)
{
	/* Check the warm reset signal from servo board */
	static int warm_reset, last;

	warm_reset = !gpio_get_level(GPIO_AP_RESET_L);

	if (last == warm_reset)
		return;

	if (warm_reset)
		chipset_reset(0); /* Warm reset AP */

	last = warm_reset;
}
DECLARE_HOOK(HOOK_SECOND, check_ap_reset_second, HOOK_PRIO_DEFAULT);
#endif
