/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* MT SCP RV32i configuration */

#include "cache.h"
#include "csr.h"
#include "registers.h"

#define SCP_SRAM_END (CONFIG_IPC_SHARED_OBJ_ADDR & (~(0x400 - 1)))

struct mpu_entry mpu_entries[NR_MPU_ENTRIES] = {
	/* SRAM (for most code, data) */
	{0, SCP_SRAM_END, MPU_ATTR_C | MPU_ATTR_W | MPU_ATTR_R},
	/* SRAM (for IPI shared buffer) */
	{SCP_SRAM_END, SCP_FW_END, MPU_ATTR_W | MPU_ATTR_R},
	/* For AP domain */
#ifdef CHIP_VARIANT_MT8195
	{0x60000000, 0x70000000, MPU_ATTR_W | MPU_ATTR_R | MPU_ATTR_P},
#else
	{0x60000000, 0x70000000, MPU_ATTR_W | MPU_ATTR_R},
#endif
	/* For SCP sys */
	{0x70000000, 0x80000000, MPU_ATTR_W | MPU_ATTR_R},
#ifdef CHIP_VARIANT_MT8195
	{0x10000000, 0x11400000, MPU_ATTR_C | MPU_ATTR_W | MPU_ATTR_R},
#else
	{0x10000000, 0x11400000, MPU_ATTR_W | MPU_ATTR_R},
#endif
};

#include "gpio_list.h"
