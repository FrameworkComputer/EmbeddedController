/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"

#include <zephyr/fff.h>

/* LCOV_EXCL_START */

FAKE_VOID_FUNC(power_button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(chipset_reset_request_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(power_signal_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(chipset_watchdog_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(extpower_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(usb_a0_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(switch_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(spi_event, enum gpio_signal);
FAKE_VOID_FUNC(ccd_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(x_ec_interrupt, enum gpio_signal);

/* LCOV_EXCL_STOP */
