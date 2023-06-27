/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"

/**
 * @brief Override for testing. This method simply returns `pdata_ptr` and
 *        bypasses the usual logic in `common/panic_output.c`
 *
 * @return struct panic_data* Pointer to the pdata_ptr object
 */
struct panic_data *get_panic_data_write(void)
{
	/* Test-only helper method to access `pdata_ptr` in `panic_output.c` */
	return test_get_panic_data_pointer();
}
