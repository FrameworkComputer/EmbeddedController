/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nereid sub-board hardware configuration */

#include <init.h>
#include <kernel.h>
#include <sys/printk.h>

#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_pd.h"
#include "task.h"

#include "sub_board.h"

static void nereid_subboard_init(void)
{
	enum nissa_sub_board_type sb = nissa_get_sb_type();

	/*
	 * Need to initialise board specific GPIOs since the
	 * common init code does not know about them.
	 * Remove once common code initialises all GPIOs, not just
	 * the ones with enum-names.
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
	}
	if (sb == NISSA_SB_HDMI_A) {
		/* Disable I2C_PORT_USB_C1_TCPC */
		/* TODO(b:212490923): Use pinctrl to switch from I2C */
		/* Enable HDMI GPIOs */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_en_rails_odl),
			GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl),
			GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
		/* Configure the interrupt separately */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_hpd_odl),
			GPIO_INPUT);
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
