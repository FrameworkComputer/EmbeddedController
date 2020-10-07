/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System : hardware specific implementation */

#include "csr.h"
#include "memmap.h"
#include "registers.h"
#include "system.h"

void system_pre_init(void)
{
	memmap_init();

	/* enable CPU and platform low power CG */
	/* enable CPU DCM */
	set_csr(CSR_MCTREN, CSR_MCTREN_CG);

	/* Disable jump (it has only RW) and enable MPU. */
	/* TODO: implement MPU */
	system_disable_jump();
}

void system_reset(int flags)
{
	while (1)
		;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_INVAL;
}

const char *system_get_chip_vendor(void)
{
	return "mtk";
}

const char *system_get_chip_name(void)
{
	/* Support only SCP_A for now */
	return "scp_a";
}

const char *system_get_chip_revision(void)
{
	return "";
}
