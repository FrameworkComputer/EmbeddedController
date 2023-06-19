/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_switchcap_gpio

#include "common.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/* TODO(b/218600962): Consolidate switchcap code. */

#define SC_PIN_ENABLE_GPIO DT_INST_PROP(0, enable_pin)
#define SC_PIN_ENABLE GPIO_DT_FROM_NODE(SC_PIN_ENABLE_GPIO)

#define SC_PIN_POWER_GOOD_GPIO DT_INST_PROP(0, power_good_pin)
#define SC_PIN_POWER_GOOD_EXISTS DT_NODE_EXISTS(SC_PIN_POWER_GOOD_GPIO)
#define SC_PIN_POWER_GOOD GPIO_DT_FROM_NODE(SC_PIN_POWER_GOOD_GPIO)

static const int32_t poff_delay_ms = DT_INST_PROP_OR(0, poff_delay_ms, 0);

void board_set_switchcap_power(int enable)
{
	gpio_pin_set_dt(SC_PIN_ENABLE, enable);
	if (!enable && poff_delay_ms > 0)
		k_msleep(poff_delay_ms);
}

int board_is_switchcap_enabled(void)
{
	return gpio_pin_get_dt(SC_PIN_ENABLE);
}

int board_is_switchcap_power_good(void)
{
#if SC_PIN_POWER_GOOD_EXISTS
	return gpio_pin_get_dt(SC_PIN_POWER_GOOD);
#else
	return 1;
#endif
}
