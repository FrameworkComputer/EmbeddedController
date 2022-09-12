/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "compile_time_macros.h"

#include "ec_panicinfo.h"

static void print_panic_reg(int regnum, const uint32_t *regs, int index)
{
	static const char *const regname[] = { "r0 ", "r1 ", "r2 ", "r3 ",
					       "r4 ", "r5 ", "r6 ", "r7 ",
					       "r8 ", "r9 ", "r10", "r11",
					       "r12", "sp ", "lr ", "pc " };

	printf("%s:", regname[regnum]);
	if (regs)
		printf("%08x", regs[index]);
	else
		printf("        ");
	printf((regnum & 3) == 3 ? "\n" : " ");
}

static void panic_show_extra_cm(const struct panic_data *pdata)
{
	enum {
		CPU_NVIC_CFSR_BFARVALID = BIT(15),
		CPU_NVIC_CFSR_MFARVALID = BIT(7),
	};

	printf("\n");
	if (pdata->cm.cfsr & CPU_NVIC_CFSR_BFARVALID)
		printf("bfar=%08x, ", pdata->cm.bfar);
	if (pdata->cm.cfsr & CPU_NVIC_CFSR_MFARVALID)
		printf("mfar=%08x, ", pdata->cm.mfar);
	printf("cfsr=%08x, ", pdata->cm.cfsr);
	printf("shcsr=%08x, ", pdata->cm.shcsr);
	printf("hfsr=%08x, ", pdata->cm.hfsr);
	printf("dfsr=%08x, ", pdata->cm.dfsr);
	printf("ipsr=%08x", pdata->cm.regs[CORTEX_PANIC_REGISTER_IPSR]);
	printf("\n");
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
	const char *panic_origins[3] = { "", "PROCESS", "HANDLER" };

	printf("Saved panic data:%s\n",
	       (pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD ? "" : " (NEW)"));

	if (pdata->struct_version == 2)
		origin = ((lregs[11] & 0xf) == 1 || (lregs[11] & 0xf) == 9) ?
				 ORIG_HANDLER :
				 ORIG_PROCESS;

	/*
	 * In pdata struct, 'regs', which is allocated before 'frame', has
	 * one less elements in version 1. Therefore, if the data is from
	 * version 1, shift 'sregs' by one element to align with 'frame' in
	 * version 1.
	 */
	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->cm.frame - (pdata->struct_version == 1 ? 1 : 0);

	printf("=== %s EXCEPTION: %02x ====== xPSR: %08x ===\n",
	       panic_origins[origin], lregs[1] & 0xff, sregs ? sregs[7] : -1);
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

	panic_show_extra_cm(pdata);

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
	printf("R0  %08x R1  %08x R2  %08x R3  %08x\n", regs[0], regs[1],
	       regs[2], regs[3]);
	printf("R4  %08x R5  %08x R6  %08x R7  %08x\n", regs[4], regs[5],
	       regs[6], regs[7]);
	printf("R8  %08x R9  %08x R10 %08x R15 %08x\n", regs[8], regs[9],
	       regs[10], regs[11]);
	printf("FP  %08x GP  %08x LP  %08x SP  %08x\n", regs[12], regs[13],
	       regs[14], regs[15]);
	printf("IPC %08x IPSW   %05x\n", ipc, ipsw);
	printf("SWID of ITYPE: %x\n", ((itype >> 16) & 0x7fff));

	return 0;
}

static int parse_panic_info_rv32i(const struct panic_data *pdata)
{
	uint32_t *regs, mcause, mepc;

	regs = (uint32_t *)pdata->riscv.regs;
	mcause = pdata->riscv.mcause;
	mepc = pdata->riscv.mepc;

	printf("=== EXCEPTION: MCAUSE=%x ===\n", mcause);
	printf("S11 %08x S10 %08x  S9 %08x  S8   %08x\n", regs[0], regs[1],
	       regs[2], regs[3]);
	printf("S7  %08x S6  %08x  S5 %08x  S4   %08x\n", regs[4], regs[5],
	       regs[6], regs[7]);
	printf("S3  %08x S2  %08x  S1 %08x  S0   %08x\n", regs[8], regs[9],
	       regs[10], regs[11]);
	printf("T6  %08x T5  %08x  T4 %08x  T3   %08x\n", regs[12], regs[13],
	       regs[14], regs[15]);
	printf("T2  %08x T1  %08x  T0 %08x  A7   %08x\n", regs[16], regs[17],
	       regs[18], regs[19]);
	printf("A6  %08x A5  %08x  A4 %08x  A3   %08x\n", regs[20], regs[21],
	       regs[22], regs[23]);
	printf("A2  %08x A1  %08x  A0 %08x  TP   %08x\n", regs[24], regs[25],
	       regs[26], regs[27]);
	printf("GP  %08x RA  %08x  SP %08x  MEPC %08x\n", regs[28], regs[29],
	       regs[30], mepc);

	return 0;
}

int parse_panic_info(const char *data, size_t size)
{
	/* Size of the panic information "header". */
	const size_t header_size = 4;
	/* Size of the panic information "trailer" (struct_size and magic). */
	const size_t trailer_size = sizeof(struct panic_data) -
				    offsetof(struct panic_data, struct_size);

	struct panic_data pdata = { 0 };
	size_t copy_size;

	if (size < (header_size + trailer_size)) {
		fprintf(stderr, "ERROR: Panic data too short (%zd).\n", size);
		return -1;
	}

	if (size > sizeof(pdata)) {
		fprintf(stderr,
			"WARNING: Panic data too large (%zd > %zd). "
			"Following data may be incorrect!\n",
			size, sizeof(pdata));
		copy_size = sizeof(pdata);
	} else {
		copy_size = size;
	}
	/* Copy the data into pdata, as the struct size may have changed. */
	memcpy(&pdata, data, copy_size);
	/* Then copy the trailer in position. */
	memcpy((char *)&pdata + (sizeof(struct panic_data) - trailer_size),
	       data + (size - trailer_size), trailer_size);

	/*
	 * We only understand panic data with version <= 2. Warn the user
	 * of higher versions.
	 */
	if (pdata.struct_version > 2)
		fprintf(stderr,
			"WARNING: Unknown panic data version (%d). "
			"Following data may be incorrect!\n",
			pdata.struct_version);

	/* Validate magic number */
	if (pdata.magic != PANIC_DATA_MAGIC)
		fprintf(stderr,
			"WARNING: Incorrect panic magic (%d). "
			"Following data may be incorrect!\n",
			pdata.magic);

	if (pdata.struct_size != size)
		fprintf(stderr,
			"WARNING: Panic struct size inconsistent (%u vs %zd). "
			"Following data may be incorrect!\n",
			pdata.struct_size, size);

	switch (pdata.arch) {
	case PANIC_ARCH_CORTEX_M:
		return parse_panic_info_cm(&pdata);
	case PANIC_ARCH_NDS32_N8:
		return parse_panic_info_nds32(&pdata);
	case PANIC_ARCH_RISCV_RV32I:
		return parse_panic_info_rv32i(&pdata);
	default:
		fprintf(stderr, "ERROR: Unknown architecture (%d).\n",
			pdata.arch);
		break;
	}
	return -1;
}
