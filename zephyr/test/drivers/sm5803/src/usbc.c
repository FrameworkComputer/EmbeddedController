/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "driver/charger/sm5803.h"
#include "driver/tcpm/tcpci.h"
#include "emul/emul_sm5803.h"
#include "test/drivers/charger_utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

__override bool pd_check_vbus_level(int port, enum vbus_level level)
{
	return sm5803_check_vbus_level(port, level);
}

static void pin_interrupt_handler(const struct device *gpio,
				  struct gpio_callback *const cb,
				  gpio_port_pins_t pins)
{
	sm5803_interrupt(get_charger_num(&sm5803_drv));
}

static int configure_charger_interrupt(void)
{
	const struct gpio_dt_spec *gpio = sm5803_emul_get_interrupt_gpio(
		EMUL_DT_GET(DT_NODELABEL(sm5803_emul)));
	static struct gpio_callback callback;

	if (!device_is_ready(gpio->port))
		k_oops();

	gpio_emul_input_set(gpio->port, gpio->pin, 1);
	gpio_pin_configure_dt(gpio, GPIO_INPUT | GPIO_ACTIVE_LOW);
	gpio_init_callback(&callback, pin_interrupt_handler, BIT(gpio->pin));
	gpio_add_callback(gpio->port, &callback);
	gpio_pin_interrupt_configure_dt(gpio, GPIO_INT_EDGE_TO_ACTIVE);

	return 0;
}
SYS_INIT(configure_charger_interrupt, APPLICATION, 10);
