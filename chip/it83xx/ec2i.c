/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EC2I control module for IT83xx. */

#include "hooks.h"
#include "ec2i_chip.h"
#include "registers.h"
#include "timer.h"

#define EC2I_ACCESS_TIME_USEC 15

static int ec2i_check_status_bit(uint8_t status_bit)
{
	int num;

	for (num = 0; num < 10; num++) {
		udelay(EC2I_ACCESS_TIME_USEC);

		if (!(IT83XX_EC2I_IBCTL & status_bit))
			return 0;
	}

	/* Timeout */
	return -1;
}

static void ec2i_ec_access_enable(void)
{
	/*
	 * bit0: Host access to the PNPCFG registers is disabled.
	 * bit1: Host access to the BRAM registers is disabled.
	 */
	IT83XX_EC2I_LSIOHA |= 0x03;

	/* bit0: EC to I-Bus access enabled. */
	IT83XX_EC2I_IBCTL |= 0x01;

	/*
	 * Make sure that both CRIB and CWIB bits in IBCTL register
	 * are cleared.
	 * bit1: CRIB
	 * bit2: CWIB
	 */
	if (ec2i_check_status_bit(0x06))
		IT83XX_EC2I_IBCTL &= ~0x02;

	/* Enable EC access to the PNPCFG registers */
	IT83XX_EC2I_IBMAE |= 0x01;
}

static void ec2i_ec_access_disable(void)
{
	/* Disable EC access to the PNPCFG registers. */
	IT83XX_EC2I_IBMAE &= ~0x01;

	/* Diable EC to I-Bus access. */
	IT83XX_EC2I_IBCTL &= ~0x01;

	/* Enable host access */
	IT83XX_EC2I_LSIOHA &= ~0x03;
}

/* EC2I write */
enum ec2i_message ec2i_write(enum host_pnpcfg_index index, uint8_t data)
{
	/* bit1 : VCC power on */
	if (IT83XX_SWUC_SWCTL1 & 0x02) {
		/* Enable EC2I EC access */
		ec2i_ec_access_enable();

		/* Set indirect host I/O offset. (index port) */
		IT83XX_EC2I_IHIOA = 0;
		IT83XX_EC2I_IHD = index;

		/* Read the CWIB bit in IBCTL until it returns 0. */
		if (ec2i_check_status_bit(0x04)) {
			ec2i_ec_access_disable();
			return EC2I_WRITE_ERROR;
		}

		/* Set indirect host I/O offset. (data port) */
		IT83XX_EC2I_IHIOA = 1;
		IT83XX_EC2I_IHD = data;

		/* Read the CWIB bit in IBCTL until it returns 0. */
		if (ec2i_check_status_bit(0x04)) {
			ec2i_ec_access_disable();
			return EC2I_WRITE_ERROR;
		}

		/* Disable EC2I EC access */
		ec2i_ec_access_disable();

		return EC2I_WRITE_SUCCESS;
	} else {
		return EC2I_WRITE_ERROR;
	}
}

/* EC2I read */
enum ec2i_message ec2i_read(enum host_pnpcfg_index index)
{
	uint8_t data;

	/* bit1 : VCC power on */
	if (IT83XX_SWUC_SWCTL1 & 0x02) {
		/* Enable EC2I EC access */
		ec2i_ec_access_enable();

		/* Set indirect host I/O offset. (index port) */
		IT83XX_EC2I_IHIOA = 0;
		IT83XX_EC2I_IHD = index;

		/* Read the CWIB bit in IBCTL until it returns 0. */
		if (ec2i_check_status_bit(0x04)) {
			ec2i_ec_access_disable();
			return EC2I_READ_ERROR;
		}

		/* Set indirect host I/O offset. (data port) */
		IT83XX_EC2I_IHIOA = 1;

		/* This access is a read-action */
		IT83XX_EC2I_IBCTL |= 0x02;

		/* Read the CRIB bit in IBCTL until it returns 0. */
		if (ec2i_check_status_bit(0x02)) {
			ec2i_ec_access_disable();
			return EC2I_READ_ERROR;
		}

		/* Read the data from IHD register */
		data = IT83XX_EC2I_IHD;

		/* Disable EC2I EC access */
		ec2i_ec_access_disable();

		return EC2I_READ_SUCCESS + data;
	} else {
		return EC2I_READ_ERROR;
	}
}

static void pnpcfg_init(void)
{
	int table;

	for (table = 0x00; table < EC2I_SETTING_COUNT; table++) {
		if (ec2i_write(pnpcfg_settings[table].index_port,
			pnpcfg_settings[table].data_port) == EC2I_WRITE_ERROR)
				break;
	}
}
DECLARE_HOOK(HOOK_INIT, pnpcfg_init, HOOK_PRIO_DEFAULT);
