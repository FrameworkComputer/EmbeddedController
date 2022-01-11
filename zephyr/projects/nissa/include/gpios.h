/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_NISSA_GPIOS_H
#define __ZEPHYR_NISSA_GPIOS_H

#include <devicetree.h>
#include <drivers/gpio.h>

/*
 * Nissa board specific GPIOs.
 * These pins have multiple functions depending
 * on the sub-board connected.
 */
extern const struct gpio_dt_spec gpio_usb_c1_int_odl;
extern const struct gpio_dt_spec gpio_en_sub_rails_odl;
extern const struct gpio_dt_spec gpio_usb_c0_int_odl;
extern const struct gpio_dt_spec gpio_hdmi_en_sub_odl;
extern const struct gpio_dt_spec gpio_hpd_sub_odl;
extern const struct gpio_dt_spec gpio_en_sub_usb_a1_vbus;
extern const struct gpio_dt_spec gpio_sub_usb_a1_ilimit_sdp;

#endif /* __ZEPHYR_NISSA_GPIOS_H */
