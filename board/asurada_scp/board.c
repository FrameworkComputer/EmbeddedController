/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Asurada SCP configuration */

#include "cache.h"
#include "csr.h"
#include "registers.h"

struct mpu_entry mpu_entries[NR_MPU_ENTRIES] = {
	/* SRAM (for most code, data) */
	{0, 0x0ffc00, MPU_ATTR_C | MPU_ATTR_W | MPU_ATTR_R},
	/* SRAM (for IPI shared buffer) */
	{0x0ffc00, 0x100000, MPU_ATTR_W | MPU_ATTR_R},
	/* For AP domain */
	{0x60000000, 0x70000000, MPU_ATTR_W | MPU_ATTR_R},
	/* For SCP sys */
	{0x70000000, 0x80000000, MPU_ATTR_W | MPU_ATTR_R},
};

#include "gpio_list.h"
