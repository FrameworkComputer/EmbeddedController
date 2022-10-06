/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include "common.h"

/* TODO(b/218600962): Consolidate switchcap code. */

#if DT_NODE_EXISTS(DT_PATH(switchcap))

#if !DT_NODE_HAS_COMPAT(DT_PATH(switchcap), switchcap_gpio)
#error "Invalid /switchcap node in device tree"
#endif

#define SC_PIN_ENABLE_PHANDLE \
	DT_PHANDLE_BY_IDX(DT_PATH(switchcap), enable_pin, 0)
#define SC_PIN_ENABLE GPIO_DT_FROM_NODE(SC_PIN_ENABLE_PHANDLE)

#define SC_PIN_POWER_GOOD_PHANDLE \
	DT_PHANDLE_BY_IDX(DT_PATH(switchcap), power_good_pin, 0)
#define SC_PIN_POWER_GOOD_EXISTS DT_NODE_EXISTS(SC_PIN_POWER_GOOD_PHANDLE)
#define SC_PIN_POWER_GOOD GPIO_DT_FROM_NODE(SC_PIN_POWER_GOOD_PHANDLE)

#if DT_NODE_HAS_PROP(DT_PATH(switchcap), poff_delay_ms)
static const int32_t poff_delay_ms = DT_PROP(DT_PATH(switchcap), poff_delay_ms);
#else
static const int32_t poff_delay_ms;
#endif

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

#endif
