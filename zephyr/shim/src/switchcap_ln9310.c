/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "common.h"
#include "gpio.h"
#include "ln9310.h"

#if DT_NODE_EXISTS(DT_PATH(switchcap))

#if !DT_NODE_HAS_COMPAT(DT_PATH(switchcap), switchcap_ln9310)
#error "Invalid /switchcap node in device tree"
#endif

#define SC_PIN_ENABLE_L_PHANDLE \
	DT_PHANDLE_BY_IDX(DT_PATH(switchcap), enable_l_pin, 0)
#define SC_PIN_ENABLE_L \
	GPIO_SIGNAL(SC_PIN_ENABLE_L_PHANDLE)

#define SC_PORT_PHANDLE \
	DT_PHANDLE(DT_PATH(switchcap), port)
#define SC_PORT \
	DT_ENUM_UPPER_TOKEN(SC_PORT_PHANDLE, enum_name)

#define SC_ADDR_FLAGS \
	DT_ENUM_UPPER_TOKEN(DT_PATH(switchcap), addr_flags)

void board_set_switchcap_power(int enable)
{
	gpio_set_level(SC_PIN_ENABLE_L, !enable);
	ln9310_software_enable(enable);
}

int board_is_switchcap_enabled(void)
{
	return !gpio_get_level(SC_PIN_ENABLE_L);
}

int board_is_switchcap_power_good(void)
{
	return ln9310_power_good();
}

const struct ln9310_config_t ln9310_config = {
	.i2c_port = SC_PORT,
	.i2c_addr_flags = SC_ADDR_FLAGS,
};

#endif
