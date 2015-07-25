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
#include "task.h"
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
#define TPM_DATA_FIFO	     0x24
#define TPM_INTERFACE_ID     0x30
#define TPM_DID_VID	    0xf00
#define TPM_RID		    0xf04

#define GOOGLE_VID 0x1ae0
#define GOOGLE_DID 0x0028
#define CR50_RID	0  /* No revision ID yet */

/* Tpm state machine states. */
enum tpm_states {
	tpm_state_idle,
	tpm_state_ready,
	tpm_state_receiving_cmd,
	tpm_state_executing_cmd,
	tpm_state_completing_cmd,
};

/* A preliminary interface capability register value, will be fine tuned. */
#define IF_CAPABILITY_REG ((3 << 28) | /* TPM2.0 (interface 1.3) */   \
			   (3 << 9) | /* up to 64 bytes transfers. */ \
			   0x15) /* Mandatory set to one. */

/* Volatile registers for FIFO mode */
struct tpm_register_file {
	uint8_t access;
	uint32_t int_status;
	uint32_t sts;
	uint8_t data_fifo[64]; /* this might have to be much deeper. */
};

/*
 * Tpm representation. This is a file scope variable, only one locality is
 * supported.
 */
static struct {
	enum tpm_states		  state;
	uint32_t fifo_read_index;   /* for read commands */
	uint32_t fifo_write_index;  /* for write commands */
	struct tpm_register_file  regs;
} tpm_;

/* Bit definitions for some TPM registers. */
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
 * Some TPM registers allow writing of only exactly one bit. This helper
 * function allows to verify that a value is compliant with this
 * requirement
 */
static int single_bit_set(uint32_t value)
{
	return value && !(value & (value - 1));
}

/*
 * NOTE: The put/get functions are called in interrupt context! Don't waste a
 * lot of time here - just copy the data and wake up a task to deal with it
 * later. Although if the implementation mandates a "busy" bit somewhere, you
 * might want to set it now to avoid race conditions with back-to-back
 * interrupts.
 */

static void copy_bytes(uint8_t *dest, uint32_t data_size, uint32_t value)
{
	unsigned i;

	data_size = MIN(data_size, 4);

	for (i = 0; i < data_size; i++)
		dest[i] = (value >> (i * 8)) & 0xff;
}

static void access_reg_write(uint8_t data)
{
	if (!single_bit_set(data)) {
		CPRINTF("%s: attempt to set acces reg to %02x\n",
			__func__, data);
		return;
	}

	switch (data) {
	case request_use:
		/*
		 * No multiple localities supported, let's just always honor
		 * this request.
		 */
		tpm_.regs.access |= active_locality;
		break;

	case active_locality:
		switch (tpm_.state) {
		case tpm_state_ready:
		case tpm_state_idle:
			break;
		default:
			/*
			 * TODO: need to decide what to do if there is a
			 * command in progress.
			 */
			CPRINTF("%s: locality release request in state %d\n",
			__func__, tpm_.state);
			break;
		}
		tpm_.regs.access &= ~active_locality;
		/* No matter what we do, fall into idle state. */
		tpm_.state = tpm_state_idle;
		tpm_.fifo_read_index = 0;
		tpm_.fifo_write_index = 0;
		break;

	default:
		CPRINTF("%s: attempt to set access reg to 0x%02x\n",
			__func__, data);
		break;
	}
}

/*
 * Process writes into the 'important' sts register bits. Actions on all
 * depends on the current state of the device.
 */
static void sts_reg_write_cr(void)
{
	switch (tpm_.state) {
	case tpm_state_idle:
		tpm_.state = tpm_state_ready;
		tpm_.regs.sts |= command_ready;
		break;
	case tpm_state_ready:
		tpm_.regs.sts |= command_ready;
		break;
	case tpm_state_completing_cmd:
	case tpm_state_executing_cmd:
	case tpm_state_receiving_cmd:
		tpm_.state = tpm_state_idle;
		tpm_.regs.sts &= ~command_ready;
		break;
	}
}

static void sts_reg_write_tg(void)
{
	switch (tpm_.state) {
	case tpm_state_completing_cmd:
	case tpm_state_executing_cmd:
	case tpm_state_idle:
	case tpm_state_ready:
		break; /* Ignore setting this bit in these states. */
	case tpm_state_receiving_cmd:
		if (!(tpm_.state & expect)) {
			/* This should trigger actual command execution. */
			tpm_.state = tpm_state_executing_cmd;
			task_set_event(TASK_ID_TPM, TASK_EVENT_WAKE, 0);
		}
		break;
	}
}

static void sts_reg_write_rr(void)
{
	switch (tpm_.state) {
	case tpm_state_idle:
	case tpm_state_ready:
	case tpm_state_receiving_cmd:
	case tpm_state_executing_cmd:
		break;
	case tpm_state_completing_cmd:
		tpm_.fifo_read_index = 0;
		break;
	}
}

/*
 * TPM_STS register both reports current state machine state and controls some
 * of state machine transitions.
 */
static void sts_reg_write(const uint8_t *data, uint32_t data_size)
{
	uint32_t value;

	data_size = MIN(data_size, 4);
	memcpy(&value, data, data_size);

	/* By definition only one bit can be set at a time. */
	if (!single_bit_set(value)) {
		CPRINTF("%s: attempt to set acces reg to %02x\n",
			__func__, data);
		return;
	}

	switch (value) {
	case command_ready:
		sts_reg_write_cr();
		break;
	case tpm_go:
		sts_reg_write_tg();
		break;
	case response_retry:
		sts_reg_write_rr();
		break;
	case command_cancel:
		/* TODO: this also needs to be handled, fall through for now. */
	default:
		CPRINTF("requested to write %08x to sts\n", value);
		break;
	}
}

/* We presume to be running on a little endian CPU. */
static uint32_t be32_to_cpu(const uint8_t *data)
{
	int i;
	uint32_t value = 0;

	for (i = 0; i < sizeof(value); i++)
		value |= ((uint32_t)data[i]) << (8 * (sizeof(value) - i - 1));

	return value;
}

/* Collect received data in the local buffer and change state accordingly. */
static void fifo_reg_write(const uint8_t *data, uint32_t data_size)
{
	uint32_t packet_size;
	/*
	 * Make sure we are in the approriate sate, otherwise ignore this
	 * access.
	 */
	if ((tpm_.state == tpm_state_ready) && (tpm_.fifo_write_index == 0))
		tpm_.state = tpm_state_receiving_cmd;

	if (tpm_.state != tpm_state_receiving_cmd) {
		CPRINTF("%s: ignoring data in state %d\n",
			__func__, tpm_.state);
		return;
	}

	if ((tpm_.fifo_write_index + data_size) > sizeof(tpm_.regs.data_fifo)) {
		CPRINTF("%s: receive buffer overflow: %d in addition to %d\n",
			__func__, data_size, tpm_.fifo_write_index);
		tpm_.fifo_write_index = 0;
		tpm_.state = tpm_state_ready;
		return;
	}

	/* Copy data into the local buffer. */
	memcpy(tpm_.regs.data_fifo + tpm_.fifo_write_index,
	       data, data_size);

	tpm_.fifo_write_index += data_size;

	/* Verify that size in the header matches the block size */
	if (tpm_.fifo_write_index < 6) {
		tpm_.regs.sts |= expect; /* More data is needed. */
		return;
	}

	packet_size = be32_to_cpu(tpm_.regs.data_fifo + 2);
	if (tpm_.fifo_write_index < packet_size) {
		tpm_.regs.sts |= expect; /* More data is needed. */
		return;
	}

	/* All data has been receved, Ready for the 'go' command. */
	tpm_.regs.sts &= ~expect;
}

void tpm_register_put(uint32_t regaddr, const uint8_t *data, uint32_t data_size)
{
	uint32_t i;
	switch (regaddr) {
	case TPM_ACCESS:
		/* This is a one byte register, ignore extra data, if any */
		access_reg_write(data[0]);
		break;
	case TPM_STS:
		sts_reg_write(data, data_size);
		break;
	case TPM_DATA_FIFO:
		fifo_reg_write(data, data_size);
		break;
	default:
		CPRINTF("%s(0x%06x, %d bytes:", __func__, regaddr, data_size);
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
		copy_bytes(dest, data_size, tpm_.regs.access);
		break;
	case TPM_STS:
		copy_bytes(dest, data_size, tpm_.regs.sts);
		break;
	default:
		CPRINTS("%s(0x%06x, %d) => ??", __func__, regaddr, data_size);
		return;
	}
}


static void tpm_init(void)
{
	tpm_.state = tpm_state_idle;
	tpm_.regs.access = tpm_reg_valid_sts;
	tpm_.regs.sts = (tpm_family_tpm2 << tpm_family_shift) |
		(64 << burst_count_shift);
}

void tpm_task(void)
{
	tpm_init();
	sps_tpm_enable();
	while (1) {
		/* Wait for the next command event */
		task_wait_event(-1);
		CPRINTF("%s: received fifo command 0x%04x\n",
			__func__, be32_to_cpu(tpm_.regs.data_fifo + 6));

	}
}

