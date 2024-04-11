/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"

#include <zephyr/fff.h>

FAKE_VOID_FUNC(power_button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(chipset_reset_request_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(power_signal_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(chipset_watchdog_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(extpower_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(switch_interrupt, enum gpio_signal);
#if (!(defined(CONFIG_TEST_KINGLER_USBC) ||             \
       defined(CONFIG_TEST_DB_DETECTION_USB_COUNT))) && \
	!defined(CONFIG_TEST_CORSOLA_USBC)
FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);
#endif
#if !defined(CONFIG_TEST_KINGLER_USBC) && !defined(CONFIG_TEST_CORSOLA_USBC)
FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(usb_a0_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(tcpc_alert_event, enum gpio_signal);
FAKE_VOID_FUNC(ppc_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bc12_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(pd_power_supply_reset, int);
FAKE_VOID_FUNC(pd_set_power_supply_ready, int);
FAKE_VALUE_FUNC(int, pd_snk_is_vbus_provided, int);
FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
#endif
FAKE_VOID_FUNC(chipset_warm_reset_interrupt, enum gpio_signal);
#ifndef CONFIG_TEST_KINGLER_CCD
FAKE_VOID_FUNC(ccd_interrupt, enum gpio_signal);
#endif

#if (defined(CONFIG_TEST_STEELIX_RUSTY) || defined(CONFIG_TEST_PONYTA) || \
     defined(CONFIG_TEST_SQUIRTLE))
#ifndef CONFIG_TEST_SQUIRTLE
FAKE_VOID_FUNC(x_ec_interrupt, enum gpio_signal);
#endif
FAKE_VOID_FUNC(motion_sensors_check_ssfc);
#else
FAKE_VOID_FUNC(motion_interrupt, enum gpio_signal);
#endif

#ifdef CONFIG_VARIANT_CORSOLA_DB_DETECTION
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
#endif
