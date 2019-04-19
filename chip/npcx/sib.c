/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific SIB module for Chrome EC */

#include "console.h"
#include "hwtimer_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * Timeout to wait for host transaction to be completed.
 *
 * For eSPI - it is 200 us.
 * For LPC - it is 5 us.
 */
#ifdef CONFIG_HOSTCMD_ESPI
#define HOST_TRANSACTION_TIMEOUT_US 200
#else
#define HOST_TRANSACTION_TIMEOUT_US 5
#endif

/* Console output macros */
#ifdef DEBUG_SIB
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#else
#define CPUTS(...)
#define CPRINTS(...)
#endif

/*
 * Check host read is not in-progress and no timeout
 */
static void sib_wait_host_read_done(void)
{
	timestamp_t deadline, start;

	start = get_time();
	deadline.val = start.val + HOST_TRANSACTION_TIMEOUT_US;
	while (IS_BIT_SET(NPCX_SIBCTRL, NPCX_SIBCTRL_CSRD)) {
		if (timestamp_expired(deadline, NULL)) {
			CPRINTS("Unexpected time of host read transaction");
			break;
		}
		/* Handle ITIM32 overflow condition */
		__hw_clock_handle_overflow(start.le.hi);
	}
}

/*
 * Check host write is not in-progress and no timeout
 */
static void sib_wait_host_write_done(void)
{
	timestamp_t deadline, start;

	start = get_time();
	deadline.val = start.val + HOST_TRANSACTION_TIMEOUT_US;
	while (IS_BIT_SET(NPCX_SIBCTRL, NPCX_SIBCTRL_CSWR)) {
		if (timestamp_expired(deadline, NULL)) {
			CPRINTS("Unexpected time of host write transaction");
			break;
		}
		/* Handle ITIM32 overflow condition */
		__hw_clock_handle_overflow(start.le.hi);
	}
}

/* Emulate host to read Keyboard I/O */
uint8_t sib_read_kbc_reg(uint8_t io_offset)
{
	uint8_t data_value;

	/* Disable interrupts */
	interrupt_disable();

	/* Lock host keyboard module */
	SET_BIT(NPCX_LKSIOHA, NPCX_LKSIOHA_LKHIKBD);
	/* Verify Core read/write to host modules is not in progress */
	sib_wait_host_read_done();
	sib_wait_host_write_done();
	/* Enable Core access to keyboard module */
	SET_BIT(NPCX_CRSMAE, NPCX_CRSMAE_HIKBDAE);

	/* Specify the io_offset A0 = 0. the index register is accessed */
	NPCX_IHIOA = io_offset;

	/* Start a Core read from host module */
	SET_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSRD);
	/* Wait while Core read operation is in progress */
	sib_wait_host_read_done();
	/* Read the data */
	data_value = NPCX_IHD;

	/* Disable Core access to keyboard module */
	CLEAR_BIT(NPCX_CRSMAE, NPCX_CRSMAE_HIKBDAE);
	/* unlock host keyboard module */
	CLEAR_BIT(NPCX_LKSIOHA, NPCX_LKSIOHA_LKHIKBD);

	/* Enable interrupts */
	interrupt_enable();

	return data_value;
}

/* Super-IO read/write function */
void sib_write_reg(uint8_t io_offset, uint8_t index_value,
		uint8_t io_data)
{
	/* Disable interrupts */
	interrupt_disable();

	/* Lock host CFG module */
	SET_BIT(NPCX_LKSIOHA, NPCX_LKSIOHA_LKCFG);
	/* Enable Core access to CFG module */
	SET_BIT(NPCX_CRSMAE, NPCX_CRSMAE_CFGAE);
	/* Verify Core read/write to host modules is not in progress */
	sib_wait_host_read_done();
	sib_wait_host_write_done();

	/* Specify the io_offset A0 = 0. the index register is accessed */
	NPCX_IHIOA = io_offset;
	/* Write the data. This starts the write access to the host module */
	NPCX_IHD = index_value;
	/* Wait while Core write operation is in progress */
	sib_wait_host_write_done();

	/* Specify the io_offset A0 = 1. the data register is accessed */
	NPCX_IHIOA = io_offset+1;
	/* Write the data. This starts the write access to the host module */
	NPCX_IHD = io_data;
	/* Wait while Core write operation is in progress */
	sib_wait_host_write_done();

	/* Disable Core access to CFG module */
	CLEAR_BIT(NPCX_CRSMAE, NPCX_CRSMAE_CFGAE);
	/* unlock host CFG  module */
	CLEAR_BIT(NPCX_LKSIOHA, NPCX_LKSIOHA_LKCFG);

	/* Enable interrupts */
	interrupt_enable();
}

uint8_t sib_read_reg(uint8_t io_offset, uint8_t index_value)
{
	uint8_t data_value;

	/* Disable interrupts */
	interrupt_disable();

	/* Lock host CFG module */
	SET_BIT(NPCX_LKSIOHA, NPCX_LKSIOHA_LKCFG);
	/* Enable Core access to CFG module */
	SET_BIT(NPCX_CRSMAE, NPCX_CRSMAE_CFGAE);
	/* Verify Core read/write to host modules is not in progress */
	sib_wait_host_read_done();
	sib_wait_host_write_done();

	/* Specify the io_offset A0 = 0. the index register is accessed */
	NPCX_IHIOA = io_offset;
	/* Write the data. This starts the write access to the host module */
	NPCX_IHD = index_value;
	/* Wait while Core write operation is in progress */
	sib_wait_host_write_done();

	/* Specify the io_offset A0 = 1. the data register is accessed */
	NPCX_IHIOA = io_offset+1;
	/* Start a Core read from host module */
	SET_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSRD);
	/* Wait while Core read operation is in progress */
	sib_wait_host_read_done();
	/* Read the data */
	data_value = NPCX_IHD;

	/* Disable Core access to CFG module */
	CLEAR_BIT(NPCX_CRSMAE, NPCX_CRSMAE_CFGAE);
	/* unlock host CFG  module */
	CLEAR_BIT(NPCX_LKSIOHA, NPCX_LKSIOHA_LKCFG);

	/* Enable interrupts */
	interrupt_enable();

	return data_value;
}

