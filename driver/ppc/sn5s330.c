/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI SN5S330 USB-C Power Path Controller */

/*
 * PP1 : Sourcing power path.
 * PP2 : Sinking power path.
 */

#include "common.h"
#include "console.h"
#include "driver/ppc/sn5s330.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#ifdef CONFIG_CMD_PPC_DUMP
static int command_sn5s330_dump(int argc, char **argv)
{
	int i;
	int data;
	int chip_idx;
	int port;
	int addr;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	chip_idx = atoi(argv[1]);
	if (chip_idx >= sn5s330_cnt)
		return EC_ERROR_PARAM1;

	port = sn5s330_chips[chip_idx].i2c_port;
	addr = sn5s330_chips[chip_idx].i2c_addr;

	for (i = SN5S330_FUNC_SET1; i <= SN5S330_FUNC_SET12; i++) {
		i2c_read8(port, addr, i, &data);
		ccprintf("FUNC_SET%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_FUNC_SET1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_STATUS_REG1; i <= SN5S330_INT_STATUS_REG4; i++) {
		i2c_read8(port, addr, i, &data);
		ccprintf("INT_STATUS_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_STATUS_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_TRIP_RISE_REG1; i <= SN5S330_INT_TRIP_RISE_REG3;
	     i++) {
		i2c_read8(port, addr, i, &data);
		ccprintf("INT_TRIP_RISE_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_TRIP_RISE_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_TRIP_FALL_REG1; i <= SN5S330_INT_TRIP_FALL_REG3;
	     i++) {
		i2c_read8(port, addr, i, &data);
		ccprintf("INT_TRIP_FALL_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_TRIP_FALL_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_MASK_RISE_REG1; i <= SN5S330_INT_MASK_RISE_REG3;
	     i++) {
		i2c_read8(port, addr, i, &data);
		ccprintf("INT_MASK_RISE_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_MASK_RISE_REG1 + 1,
			 i,
			 data);
	}

	for (i = SN5S330_INT_MASK_FALL_REG1; i <= SN5S330_INT_MASK_FALL_REG3;
	     i++) {
		i2c_read8(port, addr, i, &data);
		ccprintf("INT_MASK_FALL_REG%d [%02Xh] = 0x%02x\n",
			 i - SN5S330_INT_MASK_FALL_REG1 + 1,
			 i,
			 data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ppc_dump, command_sn5s330_dump,
			"<Type-C port>", "dump the SN5S330 regs");
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

int sn5s330_pp_fet_enable(uint8_t chip_idx, enum sn5s330_pp_idx pp, int enable)
{
	int regval;
	int status;
	int pp_bit;
	int port;
	int addr;

	if (pp == SN5S330_PP1) {
		pp_bit = SN5S330_PP1_EN;
	} else if (pp == SN5S330_PP2) {
		pp_bit = SN5S330_PP2_EN;
	} else {
		CPRINTF("bad PP idx(%d)!", pp);
		return EC_ERROR_INVAL;
	}

	port = sn5s330_chips[chip_idx].i2c_port;
	addr = sn5s330_chips[chip_idx].i2c_addr;

	status = i2c_read8(port, addr, SN5S330_FUNC_SET3, &regval);
	if (status) {
		CPRINTS("Failed to read FUNC_SET3!");
		return status;
	}

	if (enable)
		regval |= pp_bit;
	else
		regval &= ~pp_bit;

	status = i2c_write8(port, addr, SN5S330_FUNC_SET3, regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET3!");
		return status;
	}

	return EC_SUCCESS;
}

static int init_sn5s330(int idx)
{
	int regval;
	int status;
	int retries;
	int i2c_port;
	int i2c_addr;
	int reg;

	i2c_port = sn5s330_chips[idx].i2c_port;
	i2c_addr = sn5s330_chips[idx].i2c_addr;

	/* Set the sourcing current limit value. */
#if defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) &&			\
	(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT == TYPEC_RP_3A0)
	/* Set current limit to ~3A. */
	regval = SN5S330_ILIM_3_06;
#else
	/* Set current limit to ~1.5A. */
	regval = SN5S330_ILIM_1_62;
#endif

	/*
	 * It seems that sometimes setting the FUNC_SET1 register fails
	 * initially.  Therefore, we'll retry a couple of times.
	 */
	retries = 0;
	do {
		status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET1,
				    regval);
		if (status) {
			CPRINTS("Failed to set FUNC_SET1! Retrying...");
			retries++;
			msleep(1);
		} else {
			break;
		}
	} while (retries < 10);

	/* Set Vbus OVP threshold to ~22.325V. */
	regval = 0x37;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET5, regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET5!");
		return status;
	}

	/* Set Vbus UVP threshold to ~2.75V. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_FUNC_SET6, &regval);
	if (status) {
		CPRINTS("Failed to read FUNC_SET6!");
		return status;
	}
	regval &= ~0x3F;
	regval |= 1;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET6, regval);
	if (status) {
		CPRINTS("Failed to write FUNC_SET6!");
		return status;
	}

	/* Enable SBU Fets and set PP2 current limit to ~3A. */
	regval = SN5S330_SBU_EN | 0xf;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET2, regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET2!");
		return status;
	}

	/* TODO(aaboagye): What about Vconn */

	/*
	 * Indicate we are using PP2 configuration 2 and enable OVP comparator
	 * for CC lines.
	 */
	regval = SN5S330_OVP_EN_CC | SN5S330_PP2_CONFIG;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET9, regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET9!");
		return status;
	}

	/* Set analog current limit delay to 200 us for both PP1 & PP2. */
	regval = (PPX_ILIM_DEGLITCH_0_US_200 << 3) | PPX_ILIM_DEGLITCH_0_US_200;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET11,
			    regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET11");
		return status;
	}

	/* Turn off dead battery resistors and turn on CC FETs. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_FUNC_SET4, &regval);
	if (status) {
		CPRINTS("Failed to read FUNC_SET4!");
		return status;
	}
	regval |= SN5S330_CC_EN;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET4, regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET4!");
		return status;
	}

	/* Set ideal diode mode for both PP1 and PP2. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_FUNC_SET3, &regval);
	if (status) {
		CPRINTS("Failed to read FUNC_SET3!");
		return status;
	}
	regval |= SN5S330_SET_RCP_MODE_PP1 | SN5S330_SET_RCP_MODE_PP2;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_FUNC_SET3, regval);
	if (status) {
		CPRINTS("Failed to set FUNC_SET3!");
		return status;
	}

	/* Turn off PP1 FET. */
	status = sn5s330_pp_fet_enable(idx, SN5S330_PP1, 0);
	if (status) {
		CPRINTS("Failed to turn off PP1 FET!");
	}

	/*
	 * Don't proceed with the rest of initialization if we're sysjumping.
	 * We would have already done this before.
	 */
	if (system_jumped_to_this_image())
		return EC_SUCCESS;

	/* Clear the digital reset bit. */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			   &regval);
	if (status) {
		CPRINTS("Failed to read INT_STATUS_REG4!");
		return status;
	}
	regval |= SN5S330_DIG_RES;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			    regval);
	if (status) {
		CPRINTS("Failed to write INT_STATUS_REG4!");
		return status;
	}

	/*
	 * Before turning on the PP2 FET, let's mask off all interrupts except
	 * for the PP1 overcurrent condition and then clear all pending
	 * interrupts.
	 *
	 * TODO(aaboagye): Unmask fast-role swap events once fast-role swap is
	 * implemented in the PD stack.
	 */

	regval = ~SN5S330_ILIM_PP1_RISE_MASK;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_RISE_REG1,
			    regval);
	if (status) {
		CPRINTS("Failed to write INT_MASK_RISE1!");
		return status;
	}

	regval = ~SN5S330_ILIM_PP1_FALL_MASK;
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_FALL_REG1,
			    regval);
	if (status) {
		CPRINTS("Failed to write INT_MASK_FALL1!");
		return status;
	}

	/* Now mask all the other interrupts. */
	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_RISE_REG2,
			    0xFF);
	if (status) {
		CPRINTS("Failed to write INT_MASK_RISE2!");
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_FALL_REG2,
			    0xFF);
	if (status) {
		CPRINTS("Failed to write INT_MASK_FALL2!");
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_RISE_REG3,
			    0xFF);
	if (status) {
		CPRINTS("Failed to write INT_MASK_RISE3!");
		return status;
	}

	status = i2c_write8(i2c_port, i2c_addr, SN5S330_INT_MASK_FALL_REG3,
			    0xFF);
	if (status) {
		CPRINTS("Failed to write INT_MASK_FALL3!");
		return status;
	}

	/* Now clear any pending interrupts. */
	for (reg = SN5S330_INT_TRIP_RISE_REG1;
	     reg <= SN5S330_INT_TRIP_FALL_REG3;
	     reg++) {
		status = i2c_write8(i2c_port, i2c_addr, reg, 0xFF);
		if (status) {
			CPRINTS("Failed to write reg 0x%2x!");
			return status;
		}
	}


	/*
	 * For PP2, check to see if we booted in dead battery mode.  If we
	 * booted in dead battery mode, the PP2 FET will already be enabled.
	 */
	status = i2c_read8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			   &regval);
	if (status) {
		CPRINTS("Failed to read INT_STATUS_REG4!");
		return status;
	}

	if (regval & SN5S330_DB_BOOT) {
		/* Clear the bit. */
		i2c_write8(i2c_port, i2c_addr, SN5S330_INT_STATUS_REG4,
			   SN5S330_DB_BOOT);

		/* Turn on PP2 FET. */
		status = sn5s330_pp_fet_enable(idx, SN5S330_PP2, 1);
		if (status) {
			CPRINTS("Failed to turn on PP2 FET!");
			return status;
		}
	}

	return EC_SUCCESS;
}

static void sn5s330_init(void)
{
	int i;
	int rv;

	for (i = 0; i < sn5s330_cnt; i++) {
		rv = init_sn5s330(i);
		if (!rv)
			CPRINTS("C%d: SN5S330 init done.", i);
		else
			CPRINTS("C%d: SN5S330 init failed! (%d)", i, rv);
	}
}
DECLARE_HOOK(HOOK_INIT, sn5s330_init, HOOK_PRIO_LAST);
