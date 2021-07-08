/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	/*
	 * Because our reset vector is at the beginning of image copy
	 * (see init.S). So I just need to return 'base' here and EC will jump
	 * to the reset vector.
	 */
	return base;
}
