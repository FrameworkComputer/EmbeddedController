/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include "gpio_signal.h"

FAKE_VOID_FUNC(power_button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(chipset_reset_request_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(power_signal_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(chipset_watchdog_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(extpower_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(usb_a0_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(switch_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(tcpc_alert_event, enum gpio_signal);
FAKE_VOID_FUNC(ppc_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bc12_interrupt, enum gpio_signal);

#ifdef CONFIG_TEST_STEELIX_RUSTY
FAKE_VOID_FUNC(x_ec_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(motion_sensors_check_ssfc);
#endif

#ifdef CONFIG_VARIANT_CORSOLA_DB_DETECTION
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
#endif
