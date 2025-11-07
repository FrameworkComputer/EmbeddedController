/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#include <ap_power/ap_power.h>

static bool value_en;
static bool boot_flags;

static void set_tp_vtsp_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_vtsp_en), value_en);
}
DECLARE_DEFERRED(set_tp_vtsp_en_pin);

static void set_tp_rst_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_rst), value_en);
}
DECLARE_DEFERRED(set_tp_rst_pin);

static void set_tp_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en), value_en);
}
DECLARE_DEFERRED(set_tp_en_pin);

/* Power-on */
static void board_tp_startup(void)
{
	value_en = 1;
	boot_flags = true;
	hook_call_deferred(&set_tp_vtsp_en_pin_data, 0);
	hook_call_deferred(&set_tp_rst_pin_data, 10 * USEC_PER_MSEC);
	hook_call_deferred(&set_tp_en_pin_data, 3080 * USEC_PER_MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_tp_startup, HOOK_PRIO_DEFAULT);

/* Power-off */
static void board_tp_shutdown(void)
{
	value_en = 0;
	hook_call_deferred(&set_tp_en_pin_data, 0);
	hook_call_deferred(&set_tp_rst_pin_data, 35 * USEC_PER_MSEC);
	hook_call_deferred(&set_tp_vtsp_en_pin_data, 70 * USEC_PER_MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_tp_shutdown, HOOK_PRIO_DEFAULT);

/* Exit suspend */
static void board_tp_resume(void)
{
	/* Avoid invoking HOOK_CHIPSET_RESUME callbacks during system boot */
	if (boot_flags) {
		boot_flags = false;
		return;
	}

	value_en = 1;
	hook_call_deferred(&set_tp_en_pin_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_tp_resume, HOOK_PRIO_DEFAULT);

/* Go to suspend */
static void board_tp_suspend(void)
{
	value_en = 0;
	hook_call_deferred(&set_tp_en_pin_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_tp_suspend, HOOK_PRIO_DEFAULT);
