/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_function.h"
#include "cpu_power.h"

__override void board_enter_non_acpi_mode(void)
{
	pmf_table_switch_by_cpu_type();
}
