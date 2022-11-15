/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT lion_ln9310

#include "common.h"
#include "ln9310.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

/* TODO(b/218600962): Consolidate switchcap code. */

#define SC_PIN_ENABLE_GPIO DT_INST_PROP(0, enable_pin)
#define SC_PIN_ENABLE GPIO_DT_FROM_NODE(SC_PIN_ENABLE_GPIO)

#define SC_PORT_NODE DT_INST_PHANDLE(0, port)
#define SC_PORT DT_STRING_UPPER_TOKEN_BY_IDX(SC_PORT_NODE, enum_names, 0)

#define SC_ADDR_FLAGS DT_INST_STRING_UPPER_TOKEN(0, addr_flags)

void board_set_switchcap_power(int enable)
{
	gpio_pin_set_dt(SC_PIN_ENABLE, enable);
	ln9310_software_enable(enable);
}

int board_is_switchcap_enabled(void)
{
	return gpio_pin_get_dt(SC_PIN_ENABLE);
}

int board_is_switchcap_power_good(void)
{
	return ln9310_power_good();
}

const struct ln9310_config_t ln9310_config = {
	.i2c_port = SC_PORT,
	.i2c_addr_flags = SC_ADDR_FLAGS,
};
