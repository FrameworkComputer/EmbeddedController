/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nissa sub-board hardware configuration */

#include <ap_power/ap_power.h>
#include <drivers/gpio.h>
#include <init.h>
#include <kernel.h>
#include <sys/printk.h>

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

/**
 * Configure GPIOs (and other pin functions) that vary with present sub-board.
 *
 * The functions of some pins vary according to which sub-board is present
 * (indicated by CBI fw_config); this function configures them according to the
 * needs of the present sub-board.
 */
static int nissa_subboard_config(const struct device *unused)
{
	ARG_UNUSED(unused);
	enum nissa_sub_board_type sb = nissa_get_sb_type();

	/*
	 * USB-A port: current limit output is configured by default and unused
	 * if this port is not present. VBUS enable must be configured if
	 * needed and is controlled by the usba-port-enable-pins driver.
	 */
	if (sb == NISSA_SB_C_A || sb == NISSA_SB_HDMI_A) {
		/* Configure VBUS enable, default off */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
			GPIO_OUTPUT_LOW);
	} else {
		/* Turn off unused pins */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_sub_usb_a1_ilimit_sdp),
			GPIO_DISCONNECTED);
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
			GPIO_DISCONNECTED);
	}
	/*
	 * USB-C port: I2C runs over two of the sub-board lines, the interrupt
	 * input needs to be configured, and USB mux configuration provided.
	 */
	if (sb == NISSA_SB_C_A || sb == NISSA_SB_C_LTE) {
		nissa_configure_c1_sb_i2c();
		/* Configure interrupt input */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl),
			GPIO_INPUT | GPIO_PULL_UP);
		usb_muxes[1].next_mux = nissa_get_c1_sb_mux();
	} else {
		/* Disable the port 1 charger task */
		task_disable_task(TASK_ID_USB_CHG_P1);
	}
	/*
	 * HDMI: two outputs control power which must be configured to
	 * non-default settings, and HPD must be forwarded to the AP on
	 * another output pin.
	 */
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

	return 0;
}
SYS_INIT(nissa_subboard_config, APPLICATION, HOOK_PRIO_POST_FIRST);

/*
 * Enable interrupts
 */
static int board_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	/*
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
	if (board_get_usb_pd_port_count() == 2)
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));

	return 0;
}
SYS_INIT(board_init, APPLICATION, HOOK_PRIO_DEFAULT);

/* Trigger shutdown by enabling the Z-sleep circuit */
__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
}
