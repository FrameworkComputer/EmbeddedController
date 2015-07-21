/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This implements the register interface for the TPM SPI Hardware Protocol.
 * The master puts or gets between 1 and 64 bytes to a register designated by a
 * 24-bit address. There is no provision for error reporting at this level.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "tpm_registers.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_TPM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_TPM, format, ## args)


/* Register addresses for FIFO mode. */
#define TPM_ACCESS		0
#define TPM_INT_ENABLE		8
#define TPM_INT_VECTOR	      0xC
#define TPM_INT_STATUS	     0x10
#define TPM_INTF_CAPABILITY  0x14
#define TPM_STS		     0x18
#define TPM_DATA_FIFO	     0x28
#define TPM_INTERFACE_ID     0x30
#define TPM_DID_VID	    0xf00
#define TPM_RID		    0xf04

#define GOOGLE_VID 0x1ae0
#define GOOGLE_DID 0x0028
#define CR50_RID	0  /* No revision ID yet */

/* A preliminary interface capability register value, will be fine tuned. */
#define IF_CAPABILITY_REG ((3 << 28) | /* TPM2.0 (interface 1.3) */   \
			   (3 << 9) | /* up to 64 bytes transfers. */ \
			   0x15) /* Mandatory set to one. */

#define MAX_LOCALITIES 1  /* Eventually there could be five. */
/* Volatile registers for FIFO mode */
static struct tpm_register_file {
	uint8_t tpm_access;
	uint32_t tpm_int_status;
	uint32_t tpm_sts;
	uint8_t tpm_data_fifo[64]; /* this might have to be much deeper. */
	uint32_t fifo_read_index;   /* for read commands */
	uint32_t fifo_write_index;  /* for write commands */
} tpm_regs;

enum tpm_access_bits {
	tpm_reg_valid_sts = (1 << 7),
	active_locality = (1 << 5),
	request_use = (1 << 1),
	tpm_establishment = (1 << 0),
};

enum tpm_sts_bits {
	tpm_family_shift = 26,
	tpm_family_mask = ((1 << 2) - 1),  /* 2 bits wide */
	tpm_family_tpm2 = 1,
	reset_establishment_bit = (1 << 25),
	command_cancel = (1 << 24),
	burst_count_shift = 8,
	burst_count_mask = ((1 << 16) - 1),  /* 16 bits wide */
	sts_valid = (1 << 7),
	command_ready = (1 << 6),
	tpm_go = (1 << 5),
	data_avail = (1 << 4),
	expect = (1 << 3),
	self_test_done = (1 << 2),
	response_retry = (1 << 1),
};

/*
 * NOTE: The put/get functions are called in interrupt context! Don't waste a
 * lot of time here - just copy the data and wake up a task to deal with it
 * later. Although if the implementation mandates a "busy" bit somewhere, you
 * might want to set it now to avoid race conditions with back-to-back
 * interrupts.
 */

static void copy_bytes(uint8_t *dest, uint32_t data_size, uint32_t value)
{
	unsigned real_size, i;

	real_size = MIN(data_size, 4);

	for (i = 0; i < real_size; i++)
		dest[i] = (value >> (i * 8)) & 0xff;
}

static void access_reg_write(uint8_t data, uint8_t *reg)
{
	/* By definition only one bit can be set at a time. */
	if (!data || (data & (data-1))) {
		CPRINTF("%s: attempt to set acces reg to %02x\n",
			__func__, data);
		return;
	}

	switch (data) {
	case request_use:
		*reg |= active_locality;
		break;

	case active_locality:
		*reg &= ~active_locality;
		break;
	}
}

void tpm_register_put(uint32_t regaddr, const uint8_t *data, uint32_t data_size)
{
	uint32_t i;
	switch (regaddr) {
	case TPM_ACCESS:
		/* This is a one byte register, ignore extra data, if any */
		access_reg_write(data[0], &tpm_regs.tpm_access);
		break;
	default:
		CPRINTF("%s(0x%06x, %d", __func__, regaddr, data_size);
		for (i = 0; i < data_size; i++)
			CPRINTF(", %02x", data[i]);
		CPRINTF("\n");
		return;
	}

}

void tpm_register_get(uint32_t regaddr, uint8_t *dest, uint32_t data_size)
{
	switch (regaddr) {
	case TPM_DID_VID:
		copy_bytes(dest, data_size, (GOOGLE_DID << 16) | GOOGLE_VID);
		break;
	case TPM_RID:
		copy_bytes(dest, data_size, CR50_RID);
		break;
	case TPM_INTF_CAPABILITY:
		copy_bytes(dest, data_size, IF_CAPABILITY_REG);
		break;
	case TPM_ACCESS:
		copy_bytes(dest, data_size, tpm_regs.tpm_access);
		break;
	case TPM_STS:
		copy_bytes(dest, data_size, tpm_regs.tpm_sts);
		break;
	default:
		CPRINTS("%s(0x%06x, %d) => ??", __func__, regaddr, data_size);
		return;
	}
}


static void tpm_init(void)
{
	tpm_regs.tpm_access = tpm_reg_valid_sts;
	tpm_regs.tpm_sts = (tpm_family_tpm2 << tpm_family_shift) |
		(64 << burst_count_shift);
}

DECLARE_HOOK(HOOK_INIT, tpm_init, HOOK_PRIO_DEFAULT);
