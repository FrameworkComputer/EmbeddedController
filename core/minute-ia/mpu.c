/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MPU module for ISH */

#include "mpu.h"
#include "console.h"
#include "registers.h"
#include "task.h"
#include "util.h"

int mpu_pre_init(void)
{
	return EC_SUCCESS;
}
