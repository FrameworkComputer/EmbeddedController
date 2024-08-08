/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>

#include <mock/ap_power_events.h>

LOG_MODULE_REGISTER(mock_ap_power_events);

DEFINE_FAKE_VALUE_FUNC(int, ap_power_ev_add_callback,
		       struct ap_power_ev_callback *);
DEFINE_FAKE_VOID_FUNC(ap_power_ev_send_callbacks, enum ap_power_events);
