/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_REX_TESTS_REX_SRC_REX_FAKES_H_
#define ZEPHYR_TEST_REX_TESTS_REX_SRC_REX_FAKES_H_

#include "charge_manager.h"
#include "gpio_signal.h"
#include "host_command.h"
#include "usb_charge.h"
#include "usb_pd.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, board_is_sourcing_vbus, int);
DECLARE_FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
DECLARE_FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
DECLARE_FAKE_VOID_FUNC(charge_manager_update_charge, int, int,
		       const struct charge_port_info *);
DECLARE_FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
DECLARE_FAKE_VOID_FUNC(pd_power_supply_reset, int);
DECLARE_FAKE_VOID_FUNC(pd_set_input_current_limit, int, uint32_t, uint32_t);

#endif /* ZEPHYR_TEST_REX_TESTS_REX_SRC_REX_FAKES_H_ */
