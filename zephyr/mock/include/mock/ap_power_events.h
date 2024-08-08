/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_MOCK_AP_POWER_EVENTS_H_
#define ZEPHYR_TEST_MOCK_AP_POWER_EVENTS_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <zephyr/fff.h>

#include <ap_power/ap_power.h>

/* Mocks for ec/zephyr/include/ap_power/ap_power_events.h */
DECLARE_FAKE_VALUE_FUNC(int, ap_power_ev_add_callback,
			struct ap_power_ev_callback *);
DECLARE_FAKE_VOID_FUNC(ap_power_ev_send_callbacks, enum ap_power_events);

void ap_power_ev_send_callbacks_custom_fake(enum ap_power_events event);

#endif /* ZEPHYR_TEST_MOCK_AP_POWER_EVENTS_H_ */
