/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "flash.h"


/**
 * Initialize the module.
 *
 * Applies at-boot protection settings if necessary.
 */
int crec_flash_pre_init(void)
{
	return EC_SUCCESS;
}
