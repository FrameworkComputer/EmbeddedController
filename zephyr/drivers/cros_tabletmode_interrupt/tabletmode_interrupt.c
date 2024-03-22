/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"
#include "tabletmode_interrupt/emul.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tabletmode_interrupt);

#define DT_DRV_COMPAT cros_tabletmode_interrupt

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Must have exactly 1 instance of this driver");

static void interrupt_handler(struct k_work *work);

static const struct gpio_dt_spec interrupt_spec =
	GPIO_DT_SPEC_GET(DT_DRV_INST(0), irq_gpios);

static struct gpio_callback interrupt_callback_data;

static K_WORK_DEFINE(interrupt_work, interrupt_handler);

static void interrupt_handler(struct k_work *work)
{
	int enable = gpio_pin_get_dt(&interrupt_spec) == 0 ? 1 : 0;

	ARG_UNUSED(work);
	tablet_set_mode(enable, TABLET_TRIGGER_LID);
}

static void interrupt_callback(const struct device *dev,
			       struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&interrupt_work);
}

int tabletmode_init_mode_interrupt(void)
{
	int ret = 0;

	if (!gpio_is_ready_dt(&interrupt_spec)) {
		ret = -EINVAL;
		goto end;
	}

	ret = gpio_pin_configure_dt(&interrupt_spec, GPIO_INPUT);
	if (ret != 0) {
		goto end;
	}

	gpio_init_callback(&interrupt_callback_data, interrupt_callback,
			   BIT(interrupt_spec.pin));

	ret = gpio_add_callback(interrupt_spec.port, &interrupt_callback_data);
	if (ret != 0) {
		goto end;
	}

	ret = gpio_pin_interrupt_configure_dt(&interrupt_spec,
					      GPIO_INT_EDGE_BOTH);
end:
	if (ret != 0) {
		LOG_ERR("device %s not ready", interrupt_spec.port->name);
	}
	return ret;
}

SYS_INIT(tabletmode_init_mode_interrupt, APPLICATION, 99);

void tabletmode_enable_peripherals(void)
{
	/*
	 * Enable keyboard when AP is running.
	 */
	keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, tabletmode_enable_peripherals,
	     HOOK_PRIO_DEFAULT);

void tabletmode_suspend_peripherals(void)
{
	/*
	 * Disable keyboard in tablet mode when AP is suspended
	 */
	if (tablet_get_mode()) {
		keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, tabletmode_suspend_peripherals,
	     HOOK_PRIO_DEFAULT);

__test_only void tabletmode_interrupt_set_device_ready(bool is_ready)
{
	struct device *dev = (struct device *)interrupt_spec.port;

	dev->state->initialized = is_ready;
}
