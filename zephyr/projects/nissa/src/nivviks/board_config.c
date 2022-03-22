/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nivviks sub-board hardware configuration */

#include <drivers/gpio.h>
#include <init.h>
#include <kernel.h>
#include <sys/printk.h>

#include "driver/charger/isl923x_public.h"
#include "driver/retimer/anx7483_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_pd.h"
#include "task.h"

#include "nissa_common.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static const struct usb_mux usbc1_anx7483 = {
	.usb_port = 1,
	.i2c_port = I2C_PORT_USB_C1_TCPC,
	.i2c_addr_flags = ANX7483_I2C_ADDR0_FLAGS,
	.driver = &anx7483_usb_retimer_driver,
};

static void nivviks_subboard_init(void)
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
		/* Enable type-C port 1 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl),
			GPIO_INPUT);
		/* Configure type-A port 1 VBUS, initialise it as low */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
			GPIO_OUTPUT_LOW);
		/* Enable redriver */
		usb_muxes[1].next_mux = &usbc1_anx7483;
	} else {
		/* Disable the port 1 charger task */
		task_disable_task(TASK_ID_USB_CHG_P1);
	}
	if (sb == NISSA_SB_HDMI_A) {
		/* Disable I2C_PORT_USB_C1_TCPC */
		/* TODO(b:212490923): Use pinctrl to switch from I2C */
		/* Enable HDMI GPIOs */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_rails_odl),
			GPIO_OUTPUT_HIGH);
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl),
			GPIO_OUTPUT_HIGH);
		/* Configure the interrupt separately */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_hpd_odl),
				      GPIO_INPUT);
	}
}
DECLARE_HOOK(HOOK_INIT, nivviks_subboard_init, HOOK_PRIO_FIRST+1);

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
		raa489000_hibernate(CHARGER_SECONDARY, true);
	raa489000_hibernate(CHARGER_PRIMARY, true);
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
