/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "ioexpanders.h"
#include "pathsel.h"

void init_pathsel(void)
{
	/* Connect TypeA port to DUT hub */
	usb3_a0_to_dut();
	/* Connect data lines */
	usb3_a0_mux_en_l(0);
	/* Enable power */
	usb3_a0_pwr_en(1);

	/* Connect TypeA port to DUT hub */
	usb3_a1_to_dut();
	/* Connect data lines */
	gpio_set_level(GPIO_USB3_A1_MUX_EN_L, 0);
	/* Enable power */
	usb3_a1_pwr_en(1);
}

void usb3_a0_to_dut(void)
{
	usb3_a0_mux_sel(1);
	gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_SEL, 1);
}

void usb3_a1_to_dut(void)
{
	usb3_a1_mux_sel(1);
	gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_SEL, 1);
}

void usb3_a0_to_host(void)
{
	usb3_a0_mux_sel(0);
}

void usb3_a1_to_host(void)
{
	usb3_a1_mux_sel(0);
}

void host_to_dut(void)
{
	gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_SEL, 0);
	uservo_fastboot_mux_sel(MUX_SEL_FASTBOOT);
}

void uservo_to_host(void)
{
	gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_SEL, 1);
	uservo_fastboot_mux_sel(MUX_SEL_USERVO);
}
