/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>

#include <mock/power_signals.h>

LOG_MODULE_REGISTER(mock_power_signals);

DEFINE_FAKE_VALUE_FUNC(int, power_signal_set, enum power_signal, int);
DEFINE_FAKE_VALUE_FUNC(int, power_signal_get, enum power_signal);
DEFINE_FAKE_VALUE_FUNC(int, power_wait_mask_signals_timeout,
		       power_signal_mask_t, power_signal_mask_t, int);
DEFINE_FAKE_VOID_FUNC(power_set_debug, power_signal_mask_t);
DEFINE_FAKE_VALUE_FUNC(power_signal_mask_t, power_get_debug);
DEFINE_FAKE_VALUE_FUNC(power_signal_mask_t, power_get_signals);
DEFINE_FAKE_VOID_FUNC(power_signal_interrupt, enum power_signal, int);
DEFINE_FAKE_VALUE_FUNC(int, power_signal_enable, enum power_signal);
DEFINE_FAKE_VALUE_FUNC(int, power_signal_disable, enum power_signal);
DEFINE_FAKE_VALUE_FUNC(const char *, power_signal_name, enum power_signal);
DEFINE_FAKE_VOID_FUNC(power_signal_init);
