/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __SKYRIM_TEST_USB_MUX_CONFIG
#define __SKYRIM_TEST_USB_MUX_CONFIG

#define WINTERHOLD_CHARGE_CURRENT_MAX 1152

#ifdef CONFIG_ZTEST

/*
 * This particular enum variant is highly entwined with the power sequencing
 * code. Define it so test can use it without pulling all that in. The
 * particular value shouldn't matter, but pick one that is unlikely to conflict
 * with any others.
 */
#define POWER_S0ix 100

#include <zephyr/fff.h>

DECLARE_FAKE_VOID_FUNC(usb_mux_enable_alternative);

#undef USB_MUX_ENABLE_ALTERNATIVE
#define USB_MUX_ENABLE_ALTERNATIVE(x) usb_mux_enable_alternative()

/* test_export_static function needed by a few tests. */
void setup_mux(void);

#endif /* CONFIG_ZTEST */

#endif /* __SKYRIM_TEST_USB_MUX_CONFIG */
