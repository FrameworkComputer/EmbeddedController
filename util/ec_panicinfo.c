/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include "compile_time_macros.h"
#include "ec_panicinfo.h"

static void print_panic_reg(int regnum, const uint32_t *regs, int index)
{
	static const char * const regname[] = {
		"r0 ", "r1 ", "r2 ", "r3 ", "r4 ",
		"r5 ", "r6 ", "r7 ", "r8 ", "r9 ",
		"r10", "r11", "r12", "sp ", "lr ",
		"pc "};

	printf("%s:", regname[regnum]);
	if (regs)
		printf("%08x", regs[index]);
	else
		printf("        ");
	printf((regnum & 3) == 3 ? "\n" : " ");
}

static int parse_panic_info_cm(const struct panic_data *pdata)
{
	const uint32_t *lregs = pdata->cm.regs;
	const uint32_t *sregs = NULL;
	enum {
		ORIG_UNKNOWN = 0,
		ORIG_PROCESS,
		ORIG_HANDLER
	} origin = ORIG_UNKNOWN;
	int i;
	const char *panic_origins[3] = {"", "PROCESS", "HANDLER"};

	printf("Saved panic data:%s\n",
	       (pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD ? "" : " (NEW)"));

	if (pdata->struct_version == 2)
		origin = ((lregs[11] & 0xf) == 1 || (lregs[11] & 0xf) == 9) ?
			 ORIG_HANDLER : ORIG_PROCESS;

	/*
	 * In pdata struct, 'regs', which is allocated before 'frame', has
	 * one less elements in version 1. Therefore, if the data is from
	 * version 1, shift 'sregs' by one element to align with 'frame' in
	 * version 1.
	 */
	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->cm.frame - (pdata->struct_version == 1 ? 1 : 0);

	printf("=== %s EXCEPTION: %02x ====== xPSR: %08x ===\n",
	       panic_origins[origin],
	       lregs[1] & 0xff, sregs ? sregs[7] : -1);
	for (i = 0; i < 4; ++i)
		print_panic_reg(i, sregs, i);
	for (i = 4; i < 10; ++i)
		print_panic_reg(i, lregs, i - 1);
	print_panic_reg(10, lregs, 9);
	print_panic_reg(11, lregs, 10);
	print_panic_reg(12, sregs, 4);
	print_panic_reg(13, lregs, origin == ORIG_HANDLER ? 2 : 0);
	print_panic_reg(14, sregs, 5);
	print_panic_reg(15, sregs, 6);

	return 0;
}

static int parse_panic_info_nds32(const struct panic_data *pdata)
{
	const uint32_t *regs = pdata->nds_n8.regs;
	uint32_t itype = pdata->nds_n8.itype;
	uint32_t ipc = pdata->nds_n8.ipc;
	uint32_t ipsw = pdata->nds_n8.ipsw;

	printf("Saved panic data:%s\n",
	       (pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD ? "" : " (NEW)"));

	printf("=== EXCEP: ITYPE=%x ===\n", itype);
	printf("R0  %08x R1  %08x R2  %08x R3  %08x\n",
		     regs[0], regs[1], regs[2], regs[3]);
	printf("R4  %08x R5  %08x R6  %08x R7  %08x\n",
		     regs[4], regs[5], regs[6], regs[7]);
	printf("R8  %08x R9  %08x R10 %08x R15 %08x\n",
		     regs[8], regs[9], regs[10], regs[11]);
	printf("FP  %08x GP  %08x LP  %08x SP  %08x\n",
		     regs[12], regs[13], regs[14], regs[15]);
	printf("IPC %08x IPSW   %05x\n", ipc, ipsw);
	printf("SWID of ITYPE: %x\n", ((itype >> 16) & 0x7fff));

	return 0;
}

int parse_panic_info(const struct panic_data *pdata)
{
	/*
	 * We only understand panic data with version <= 2. Warn the user
	 * of higher versions.
	 */
	if (pdata->struct_version > 2)
		fprintf(stderr,
			"Unknown panic data version (%d). "
			"Following data may be incorrect!\n",
			pdata->struct_version);

	/* Validate magic number */
	if (pdata->magic != PANIC_DATA_MAGIC)
		fprintf(stderr,
			"Incorrect panic magic (%d). "
			"Following data may be incorrect!\n",
			pdata->magic);

	switch (pdata->arch) {
	case PANIC_ARCH_CORTEX_M:
		return parse_panic_info_cm(pdata);
	case PANIC_ARCH_NDS32_N8:
		return parse_panic_info_nds32(pdata);
	default:
		fprintf(stderr, "Unknown architecture (%d).\n", pdata->arch);
		break;
	}
	return -1;
}
