/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gl3590.h"
#include "gpio.h"
#include "ioexpanders.h"
#include "pathsel.h"

void hh_usb3_a0_pwr_en(int en)
{
	gl3590_enable_ports(0, GL3590_DFP2, en);
}

void hh_usb3_a1_pwr_en(int en)
{
	gl3590_enable_ports(0, GL3590_DFP1, en);
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

void dut_to_host(void)
{
	gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_SEL, 0);
	gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 0);
	uservo_fastboot_mux_sel(MUX_SEL_FASTBOOT);
}

void uservo_to_host(void)
{
	uservo_fastboot_mux_sel(MUX_SEL_USERVO);
}
