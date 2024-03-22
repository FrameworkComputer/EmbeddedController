/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_ULDREN_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_ULDREN_H_

#include "ec_commands.h"

extern int battery_fuel_gauge_type_override;
extern int charge_port;

void form_factor_init(void);
void motionsense_init(void);

extern enum uldren_sub_board_type uldren_cached_sub_board;

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_ULDREN_H_ */
