/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_MOCK_POWER_SIGNALS_H
#define ZEPHYR_TEST_MOCK_POWER_SIGNALS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>

#include <zephyr/fff.h>

#include <power_signals.h>

typedef uint32_t power_signal_mask_t;

/* Mocks for ec/zephyr/subsys/ap_pwrseq/power_signals.c */
DECLARE_FAKE_VALUE_FUNC(int, power_signal_set, enum power_signal, int);
DECLARE_FAKE_VALUE_FUNC(int, power_signal_get, enum power_signal);
DECLARE_FAKE_VALUE_FUNC(int, power_wait_mask_signals_timeout,
			power_signal_mask_t, power_signal_mask_t, int);
DECLARE_FAKE_VOID_FUNC(power_set_debug, power_signal_mask_t);
DECLARE_FAKE_VALUE_FUNC(power_signal_mask_t, power_get_debug);
DECLARE_FAKE_VALUE_FUNC(power_signal_mask_t, power_get_signals);
DECLARE_FAKE_VOID_FUNC(power_signal_interrupt, enum power_signal, int);
DECLARE_FAKE_VALUE_FUNC(int, power_signal_enable, enum power_signal);
DECLARE_FAKE_VALUE_FUNC(int, power_signal_disable, enum power_signal);
DECLARE_FAKE_VALUE_FUNC(const char *, power_signal_name, enum power_signal);
DECLARE_FAKE_VOID_FUNC(power_signal_init);

int power_signal_set_custom_fake(enum power_signal signal, int value);
int power_signal_get_custom_fake(enum power_signal signal);
int power_wait_mask_signals_timeout_custom_fake(power_signal_mask_t want,
						power_signal_mask_t mask,
						int timeout);

#endif /* ZEPHYR_TEST_MOCK_POWER_SIGNALS_H */
