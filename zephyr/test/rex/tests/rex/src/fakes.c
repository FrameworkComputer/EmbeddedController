/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rex_fakes.h"

DEFINE_FAKE_VALUE_FUNC(int, board_is_sourcing_vbus, int);
DEFINE_FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
DEFINE_FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
DEFINE_FAKE_VOID_FUNC(charge_manager_update_charge, int, int,
		      const struct charge_port_info *);
DEFINE_FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
DEFINE_FAKE_VOID_FUNC(pd_power_supply_reset, int);
DEFINE_FAKE_VOID_FUNC(pd_set_input_current_limit, int, uint32_t, uint32_t);
