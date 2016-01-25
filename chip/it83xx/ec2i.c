/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EC2I control module for IT83xx. */

#include "common.h"
#include "ec2i_chip.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

/* EC2I access index/data port */
enum ec2i_access {
	/* index port */
	EC2I_ACCESS_INDEX = 0,
	/* data port */
	EC2I_ACCESS_DATA = 1,
};

enum ec2i_status_mask {
	/* 1: EC read-access is still processing. */
	EC2I_STATUS_CRIB = (1 << 1),
	/* 1: EC write-access is still processing with IHD register. */
	EC2I_STATUS_CWIB = (1 << 2),
	EC2I_STATUS_ALL  = (EC2I_STATUS_CRIB | EC2I_STATUS_CWIB),
};

static int ec2i_wait_status_bit_cleared(enum ec2i_status_mask mask)
{
	/* delay ~15.25us */
	IT83XX_GCTRL_WNCKR = 0;

	return (IT83XX_EC2I_IBCTL & mask);
}

static enum ec2i_message ec2i_write_pnpcfg(enum ec2i_access sel, uint8_t data)
{
	int rv = EC_ERROR_UNKNOWN;

	/* bit1 : VCC power on */
	if (IT83XX_SWUC_SWCTL1 & (1 << 1)) {
		/*
		 * Wait that both CRIB and CWIB bits in IBCTL register
		 * are cleared.
		 */
		rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_ALL);
		if (!rv) {
			/* Set indirect host I/O offset. */
			IT83XX_EC2I_IHIOA = sel;
			/* Write the data to IHD register */
			IT83XX_EC2I_IHD = data;
			/* Enable EC access to the PNPCFG registers */
			IT83XX_EC2I_IBMAE |= (1 << 0);
			/* bit0: EC to I-Bus access enabled. */
			IT83XX_EC2I_IBCTL |= (1 << 0);
			/* Wait the CWIB bit in IBCTL cleared. */
			rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_CWIB);
			/* Disable EC access to the PNPCFG registers. */
			IT83XX_EC2I_IBMAE &= ~(1 << 0);
			/* Disable EC to I-Bus access. */
			IT83XX_EC2I_IBCTL &= ~(1 << 0);
		}
	}

	return rv ? EC2I_WRITE_ERROR : EC2I_WRITE_SUCCESS;
}

static enum ec2i_message ec2i_read_pnpcfg(enum ec2i_access sel)
{
	int rv = EC_ERROR_UNKNOWN;
	uint8_t ihd = 0;

	/* bit1 : VCC power on */
	if (IT83XX_SWUC_SWCTL1 & (1 << 1)) {
		/*
		 * Wait that both CRIB and CWIB bits in IBCTL register
		 * are cleared.
		 */
		rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_ALL);
		if (!rv) {
			/* Set indirect host I/O offset. */
			IT83XX_EC2I_IHIOA = sel;
			/* Enable EC access to the PNPCFG registers */
			IT83XX_EC2I_IBMAE |= (1 << 0);
			/* bit1: a read-action */
			IT83XX_EC2I_IBCTL |= (1 << 1);
			/* bit0: EC to I-Bus access enabled. */
			IT83XX_EC2I_IBCTL |= (1 << 0);
			/* Wait the CRIB bit in IBCTL cleared. */
			rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_CRIB);
			/* Read the data from IHD register */
			ihd = IT83XX_EC2I_IHD;
			/* Disable EC access to the PNPCFG registers. */
			IT83XX_EC2I_IBMAE &= ~(1 << 0);
			/* Disable EC to I-Bus access. */
			IT83XX_EC2I_IBCTL &= ~(1 << 0);
		}
	}

	return rv ? EC2I_READ_ERROR : (EC2I_READ_SUCCESS + ihd);
}

/* EC2I read */
enum ec2i_message ec2i_read(enum host_pnpcfg_index index)
{
	enum ec2i_message ret = EC2I_READ_ERROR;
	uint32_t int_mask = get_int_mask();

	/* critical section with interrupts off */
	interrupt_disable();
	/* Set index */
	if (ec2i_write_pnpcfg(EC2I_ACCESS_INDEX, index) == EC2I_WRITE_SUCCESS)
		/* read data port */
		ret = ec2i_read_pnpcfg(EC2I_ACCESS_DATA);
	/* restore interrupts */
	set_int_mask(int_mask);

	return ret;
}

/* EC2I write */
enum ec2i_message ec2i_write(enum host_pnpcfg_index index, uint8_t data)
{
	enum ec2i_message ret = EC2I_WRITE_ERROR;
	uint32_t int_mask = get_int_mask();

	/* critical section with interrupts off */
	interrupt_disable();
	/* Set index */
	if (ec2i_write_pnpcfg(EC2I_ACCESS_INDEX, index) == EC2I_WRITE_SUCCESS)
		/* Set data */
		ret = ec2i_write_pnpcfg(EC2I_ACCESS_DATA, data);
	/* restore interrupts */
	set_int_mask(int_mask);

	return ret;
}

static void pnpcfg_init(void)
{
	int table;

	/* Host access is disabled */
	IT83XX_EC2I_LSIOHA |= 0x3;

	for (table = 0x00; table < EC2I_SETTING_COUNT; table++) {
		if (ec2i_write(pnpcfg_settings[table].index_port,
			pnpcfg_settings[table].data_port) == EC2I_WRITE_ERROR)
				break;
	}
}
DECLARE_HOOK(HOOK_INIT, pnpcfg_init, HOOK_PRIO_DEFAULT);
