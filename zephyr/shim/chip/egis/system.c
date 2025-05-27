/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

/* For the Egis chips, a jump instruction is placed at the beginning of firmware
 * image. Refer to start.S file for more details.
 */
uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	return base;
}
