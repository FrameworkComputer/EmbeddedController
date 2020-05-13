/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System : hardware specific implementation */

#include "registers.h"
#include "system.h"

static void scp_remap_init(void)
{
	/*
	 *  external address	SCP address
	 *
	 *  0x10000000		0x60000000
	 *  0x20000000		0x70000000
	 *  0x30000000
	 *  0x40000000
	 *  0x50000000
	 *  0x60000000		0x10000000
	 *  0x70000000		0xA0000000
	 *  0x80000000
	 *  0x90000000		0x80000000
	 *  0xA0000000		0x90000000
	 *  0xB0000000
	 *  0xC0000000		0x80000000
	 *  0xD0000000		0x20000000
	 *  0xE0000000		0x30000000
	 *  0xF0000000		0x50000000
	 */
	SCP_R_REMAP_0X0123 = 0x00070600;
	SCP_R_REMAP_0X4567 = 0x0A010000;
	SCP_R_REMAP_0X89AB = 0x00090800;
	SCP_R_REMAP_0XCDEF = 0x05030208;
}

void system_pre_init(void)
{
	scp_remap_init();
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
