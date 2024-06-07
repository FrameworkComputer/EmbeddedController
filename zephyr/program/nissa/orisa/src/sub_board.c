/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nissa sub-board hardware configuration */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "nissa_hdmi.h"
#include "nissa_sub_board.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static void hdmi_power_handler(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	/* Enable VCC on the HDMI port. */
	const struct gpio_dt_spec *s3_rail =
		GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl);

	switch (data.event) {
	case AP_POWER_STARTUP:
		LOG_DBG("Enabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 1);
		break;
	case AP_POWER_SHUTDOWN:
		LOG_DBG("Disabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 0);
		break;
	default:
		LOG_ERR("Unhandled HDMI power event %d", data.event);
		break;
	}
}

static void hdmi_hpd_interrupt(const struct device *device,
			       struct gpio_callback *callback,
			       gpio_port_pins_t pins)
{
	int state = gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_hpd_odl));

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_hdmi_hpd), state);
	LOG_DBG("HDMI HPD changed state to %d", state);
}

static void orisa_subboard_config(void)
{
	static struct ap_power_ev_callback power_cb;

	const struct gpio_dt_spec *hpd_gpio = GPIO_DT_FROM_ALIAS(gpio_hpd_odl);
	static struct gpio_callback hdmi_hpd_cb;
	int rv, irq_key;

	/*
	 * Control HDMI power according to AP power state. Some events
	 * won't do anything if the corresponding pin isn't configured,
	 * but that's okay.
	 */
	ap_power_ev_init_callback(&power_cb, hdmi_power_handler,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&power_cb);

	/*
	 * Configure HPD input from sub-board; it's inverted by a buffer
	 * on the sub-board.
	 */
	gpio_pin_configure_dt(hpd_gpio, GPIO_INPUT | GPIO_ACTIVE_LOW);
	/* Register interrupt handler for HPD changes */
	gpio_init_callback(&hdmi_hpd_cb, hdmi_hpd_interrupt,
			   BIT(hpd_gpio->pin));
	gpio_add_callback(hpd_gpio->port, &hdmi_hpd_cb);
	rv = gpio_pin_interrupt_configure_dt(hpd_gpio, GPIO_INT_EDGE_BOTH);
	__ASSERT(rv == 0, "HPD interrupt configuration returned error %d", rv);
	/*
	 * Run the HPD handler once to ensure output is in sync.
	 * Lock interrupts to ensure that we don't cause desync if an
	 * HPD interrupt comes in between the internal read of the input
	 * and write to the output.
	 */
	irq_key = irq_lock();
	hdmi_hpd_interrupt(hpd_gpio->port, &hdmi_hpd_cb, BIT(hpd_gpio->pin));
	irq_unlock(irq_key);
}
DECLARE_HOOK(HOOK_INIT, orisa_subboard_config, HOOK_PRIO_POST_FIRST);
