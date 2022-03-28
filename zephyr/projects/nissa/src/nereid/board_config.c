/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nereid sub-board hardware configuration */

#include <ap_power/ap_power.h>
#include <drivers/gpio.h>
#include <init.h>
#include <kernel.h>
#include <sys/printk.h>

#include "driver/charger/sm5803.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_pd.h"
#include "task.h"

#include "nissa_common.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static void hdmi_power_handler(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	/* Enable rails for S3 */
	const struct gpio_dt_spec *s3_rail =
		GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl);
	/* Enable rails for S5 */
	const struct gpio_dt_spec *s5_rail =
		GPIO_DT_FROM_ALIAS(gpio_en_rails_odl);
	/* Connect DDC to sub-board */
	const struct gpio_dt_spec *ddc_select =
		GPIO_DT_FROM_NODELABEL(gpio_hdmi_sel);

	switch (data.event) {
	case AP_POWER_PRE_INIT:
		LOG_DBG("Enabling HDMI+USB-A PP5000 and selecting DDC");
		gpio_pin_set_dt(s5_rail, 1);
		gpio_pin_set_dt(ddc_select, 1);
		break;
	case AP_POWER_STARTUP:
		LOG_DBG("Enabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 1);
		break;
	case AP_POWER_SHUTDOWN:
		LOG_DBG("Disabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 0);
		break;
	case AP_POWER_HARD_OFF:
		LOG_DBG("Disabling HDMI+USB-A PP5000 and deselecting DDC");
		gpio_pin_set_dt(ddc_select, 0);
		gpio_pin_set_dt(s5_rail, 0);
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

static void nereid_subboard_init(void)
{
	enum nissa_sub_board_type sb = nissa_get_sb_type();

	/*
	 * Need to initialise board specific GPIOs since the
	 * common init code does not know about them.
	 * Remove once common code initialises all GPIOs, not just
	 * the ones with enum-names.
	 *
	 * TODO(b/214858346): Enable power after AP startup.
	 */
	if (sb != NISSA_SB_C_A && sb != NISSA_SB_HDMI_A) {
		/* Turn off unused USB A1 GPIOs */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_sub_usb_a1_ilimit_sdp),
			GPIO_DISCONNECTED);
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
			GPIO_DISCONNECTED);
	}
	if (sb == NISSA_SB_C_A || sb == NISSA_SB_C_LTE) {
		static const struct usb_mux usbc1_tcpc_mux = {
			.usb_port = 1,
			.i2c_port = I2C_PORT_USB_C1_TCPC,
			.i2c_addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		};

		/* Enable type-C port 1 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl),
			GPIO_INPUT | GPIO_PULL_UP);
		/* Configure type-A port 1 VBUS, initialise it as low */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
			GPIO_OUTPUT_LOW);
		/*
		 * Use TCPC-integrated mux via CONFIG_STANDARD_OUTPUT register
		 * in PS8745.
		 */
		usb_muxes[1].next_mux = &usbc1_tcpc_mux;
	}
	if (sb == NISSA_SB_HDMI_A) {
		const struct gpio_dt_spec *hpd_gpio =
			GPIO_DT_FROM_ALIAS(gpio_hpd_odl);
		static struct ap_power_ev_callback hdmi_power_cb;
		static struct gpio_callback hdmi_hpd_cb;
		int rv, irq_key;

		/* HDMI power enable outputs */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_rails_odl),
				      GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN |
					      GPIO_PULL_UP | GPIO_ACTIVE_LOW);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl),
				      GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN |
					      GPIO_ACTIVE_LOW);
		/* Control HDMI power in concert with AP */
		ap_power_ev_init_callback(
			&hdmi_power_cb, hdmi_power_handler,
			AP_POWER_PRE_INIT | AP_POWER_HARD_OFF |
				AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
		ap_power_ev_add_callback(&hdmi_power_cb);

		/*
		 * Configure HPD input from sub-board; it's inverted by a buffer
		 * on the sub-board.
		 */
		gpio_pin_configure_dt(hpd_gpio, GPIO_INPUT | GPIO_ACTIVE_LOW);
		/* Register interrupt handler for HPD changes */
		gpio_init_callback(&hdmi_hpd_cb, hdmi_hpd_interrupt,
				   BIT(hpd_gpio->pin));
		gpio_add_callback(hpd_gpio->port, &hdmi_hpd_cb);
		rv = gpio_pin_interrupt_configure_dt(hpd_gpio,
						     GPIO_INT_EDGE_BOTH);
		__ASSERT(rv == 0,
			 "HPD interrupt configuration returned error %d", rv);
		/*
		 * Run the HPD handler once to ensure output is in sync.
		 * Lock interrupts to ensure that we don't cause desync if an
		 * HPD interrupt comes in between the internal read of the input
		 * and write to the output.
		 */
		irq_key = irq_lock();
		hdmi_hpd_interrupt(hpd_gpio->port, &hdmi_hpd_cb,
				   BIT(hpd_gpio->pin));
		irq_unlock(irq_key);
	}
}
DECLARE_HOOK(HOOK_INIT, nereid_subboard_init, HOOK_PRIO_FIRST+1);

/*
 * Enable interrupts
 */
static void board_init(void)
{
	/*
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
	if (board_get_usb_pd_port_count() == 2)
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override void board_hibernate(void)
{
	/* Shut down the chargers */
	if (board_get_usb_pd_port_count() == 2)
		sm5803_hibernate(CHARGER_SECONDARY);
	sm5803_hibernate(CHARGER_PRIMARY);
	LOG_INF("Charger(s) hibernated");
	cflush();
}

/* Trigger shutdown by enabling the Z-sleep circuit */
__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
}
