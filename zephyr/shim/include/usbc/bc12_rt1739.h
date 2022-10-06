/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/rt1739.h"

#define RT1739_BC12_COMPAT richtek_rt1739

#define BC12_CHIP_RT1739(id)             \
	{                                \
		.drv = &rt1739_bc12_drv, \
	},
