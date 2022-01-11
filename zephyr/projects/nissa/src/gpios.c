/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpios.h"

/*
 * gpio reference structures.
 */

/*
 * Unfortunately the flags in gpio_dt_spec only allow 8 bits,
 * and many of our DTS entries GPIO flags exceed that,
 * so create a new macro that only uses the lower 8 bits.
 */

#define NISSA_GPIO(x)							       \
const struct gpio_dt_spec x =						       \
{									       \
	.port = DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(x), gpios)),	       \
	.pin = DT_GPIO_PIN(DT_NODELABEL(x), gpios),			       \
	.dt_flags = 0xFF & (DT_GPIO_FLAGS(DT_NODELABEL(x), gpios)),	       \
}

NISSA_GPIO(gpio_usb_c1_int_odl);
NISSA_GPIO(gpio_en_sub_rails_odl);
NISSA_GPIO(gpio_usb_c0_int_odl);
NISSA_GPIO(gpio_hdmi_en_sub_odl);
NISSA_GPIO(gpio_hpd_sub_odl);
NISSA_GPIO(gpio_en_sub_usb_a1_vbus);
NISSA_GPIO(gpio_sub_usb_a1_ilimit_sdp);
