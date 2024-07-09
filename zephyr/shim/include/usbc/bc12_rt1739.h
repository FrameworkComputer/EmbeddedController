/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/rt1739.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT1739_BC12_COMPAT richtek_rt1739_bc12
#define RT1739_BC12_EMUL_COMPAT zephyr_rt1739_emul

#define BC12_CHIP_RT1739(id)             \
	{                                \
		.drv = &rt1739_bc12_drv, \
	},

#ifdef __cplusplus
}
#endif
