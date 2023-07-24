/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_pd.h"

#include <zephyr/fff.h>

FAKE_VOID_FUNC(pd_power_supply_reset, int);
FAKE_VOID_FUNC(pd_send_host_event, int);
FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
FAKE_VOID_FUNC(pd_set_input_current_limit, int, uint32_t, uint32_t);
FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
