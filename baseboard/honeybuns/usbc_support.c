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

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

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

#if defined(GPIO_USBC_UF_ATTACHED_SRC) && defined(SECTION_IS_RW)
static void baseboard_usb3_manage_vbus(void)
{
	int level = gpio_get_level(GPIO_USBC_UF_ATTACHED_SRC);

	/*
	 * GPIO_USBC_UF_MUX_VBUS_EN is an output from the PS8803 which tracks if
	 * C2 is attached. When it's attached, this signal will be high. Use
	 * this level to control PPC VBUS on/off.
	 */
	ppc_vbus_source_enable(USB_PD_PORT_USB3, level);
	CPRINTS("C2: State = %s", level ? "Attached.SRC " : "Unattached.SRC");
}
DECLARE_DEFERRED(baseboard_usb3_manage_vbus);

void baseboard_usb3_check_state(void)
{
	hook_call_deferred(&baseboard_usb3_manage_vbus_data, 0);
}

int baseboard_config_usbc_usb3_ppc(void)
{
	int rv;

	/*
	 * This port is not usb-pd capable, but there is a ppc which must be
	 * initialized, and keep the VBUS switch enabled.
	 */
	rv = ppc_init(USB_PD_PORT_USB3);
	if (rv)
		return rv;

	/* Need to set current limit to 3A to match advertised value */
	ppc_set_vbus_source_current_limit(USB_PD_PORT_USB3, TYPEC_RP_3A0);

	/* Check state at init time */
	baseboard_usb3_manage_vbus();

	/* Enable VBUS control interrupt for C2 */
	gpio_enable_interrupt(GPIO_USBC_UF_ATTACHED_SRC);

	return EC_SUCCESS;
}
#endif

