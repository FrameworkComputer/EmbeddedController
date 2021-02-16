/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USBC functions for RO */

#include "common.h"
#include "console.h"
#include "sn5s330.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"
#include "registers.h"
#include "ucpd-stm32gx.h"


static void baseboard_ucpd_apply_rd(int port)
{
	uint32_t cfgr1_reg;
	uint32_t moder_reg;
	uint32_t cr;

	/* Ensure that clock to UCPD is enabled */
	STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_UPCD1EN;

	/* Make sure CC1/CC2 pins PB4/PB6 are set for analog mode */
	moder_reg = STM32_GPIO_MODER(GPIO_B);
	moder_reg |= 0x3300;
	STM32_GPIO_MODER(GPIO_B) = moder_reg;
	/*
	 * CFGR1 must be written when UCPD peripheral is disabled. Note that
	 * disabling ucpd causes the peripheral to quit any ongoing activity and
	 * sets all ucpd registers back their default values.
	 */

	cfgr1_reg = STM32_UCPD_CFGR1_PSC_CLK_VAL(UCPD_PSC_DIV - 1) |
		STM32_UCPD_CFGR1_TRANSWIN_VAL(UCPD_TRANSWIN_CNT - 1) |
		STM32_UCPD_CFGR1_IFRGAP_VAL(UCPD_IFRGAP_CNT - 1) |
		STM32_UCPD_CFGR1_HBITCLKD_VAL(UCPD_HBIT_DIV - 1);
	STM32_UCPD_CFGR1(port) = cfgr1_reg;

	/* Enable ucpd  */
	STM32_UCPD_CFGR1(port) |= STM32_UCPD_CFGR1_UCPDEN;

	/* Apply Rd to both CC lines */
	cr = STM32_UCPD_CR(port);
	cr |= STM32_UCPD_CR_ANAMODE | STM32_UCPD_CR_CCENABLE_MASK;
	STM32_UCPD_CR(port) = cr;

	/*
	 * After exiting reset, stm32gx will have dead battery mode enabled by
	 * default which connects Rd to CC1/CC2. This should be disabled when EC
	 * is powered up.
	 */
	STM32_PWR_CR3 |= STM32_PWR_CR3_UCPD1_DBDIS;
}

static int read_reg(uint8_t port, int reg, int *regval)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr_flags,
			 reg,
			 regval);
}

static int write_reg(uint8_t port, int reg, int regval)
{
	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr_flags,
			  reg,
			  regval);
}

static int baseboard_ppc_enable_sink_path(int port)
{
	int regval;
	int status;
	int retries;

	/*
	 * It seems that sometimes setting the FUNC_SET1 register fails
	 * initially.  Therefore, we'll retry a couple of times.
	 */
	retries = 0;
	do {
		status = write_reg(port, SN5S330_FUNC_SET1, SN5S330_ILIM_3_06);
		if (status) {
			retries++;
			msleep(1);
		} else {
			break;
		}
	} while (retries < 10);

	/* Turn off dead battery resistors, turn on CC FETs */
	status = read_reg(port, SN5S330_FUNC_SET4, &regval);
	if (!status) {
		regval |= SN5S330_CC_EN;
		status = write_reg(port, SN5S330_FUNC_SET4, regval);
	}
	if (status) {
		return status;
	}

	/* Enable sink path via PP2 */
	status = read_reg(port, SN5S330_FUNC_SET3, &regval);
	if (!status) {
		regval &= ~SN5S330_PP1_EN;
		regval |= SN5S330_PP2_EN;
		status = write_reg(port, SN5S330_FUNC_SET3, regval);
	}
	if (status) {
		return status;
	}

	return EC_SUCCESS;
}

int baseboard_usbc_init(int port)
{
	int rv;

	/* Initialize ucpd and apply Rd to CC lines */
	baseboard_ucpd_apply_rd(port);
	/* Initialize ppc to enable sink path */
	rv = baseboard_ppc_enable_sink_path(port);

	return rv;
}

