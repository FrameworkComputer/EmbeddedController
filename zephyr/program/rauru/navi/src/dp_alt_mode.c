/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "gpio.h"
#include "usb_dp_alt_mode.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

int svdm_get_hpd_gpio(int port)
{
	/* HPD is low active, inverse the result */
	return !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l));
}

void svdm_set_hpd_gpio(int port, int en)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), !en);
}
