/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32GX UCPD module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "ucpd-stm32gx.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"


/*
 * UCPD is fed directly from HSI which is @ 16MHz. The ucpd_clk goes to
 * a prescaler who's output feeds the 'half-bit' divider which is used
 * to generate clock for delay counters and BMC Rx/Tx blocks. The rx is
 * designed to work in freq ranges of 6 <--> 18 MHz, however recommended
 * range is 9 <--> 18 MHz.
 *
 *          ------- @ 16 MHz ---------   @ ~600 kHz   -------------
 * HSI ---->| /psc |-------->| /hbit |--------------->| trans_cnt |
 *          -------          ---------    |           -------------
 *                                        |           -------------
 *                                        |---------->| ifrgap_cnt|
 *                                                    -------------
 * Requirements:
 *   1. hbit_clk ~= 600 kHz: 16 MHz / 600 kHz = 26.67
 *   2. tTransitionWindow - 12 to 20 uSec
 *   3. tInterframGap - uSec
 *
 * hbit_clk = HSI_clk / 26 = 615,385 kHz = 1.625 uSec period
 * tTransitionWindow = 1.625 uS * 8 = 13 uS
 * tInterFrameGap = 1.625 uS * 17 = 27.625 uS
 */
#define UCPD_PSC_DIV 1
#define UCPD_HBIT_DIV 26
#define UCPD_TRANSWIN_HBIT_CNT 8
#define UCPD_IFRGAP_HBIT_CNT 17

#define UCPD_ANASUB_TO_RP(r) ((r - 1) & 0x3)
#define UCPD_RP_TO_ANASUB(r) ((r + 1) & 0x3)

static void ucpd_port_enable(int port, int enable)
{
	if (enable)
		STM32_UCPD_CFGR1(port) |= STM32_UCPD_CFGR1_UCPDEN;
	else
		STM32_UCPD_CFGR1(port) &= ~STM32_UCPD_CFGR1_UCPDEN;
}

static int ucpd_is_cc_pull_active(int port, enum usbpd_cc_pin cc_line)
{
	int cc_enable = STM32_UCPD_CR(port) & STM32_UCPD_CR_CCENABLE_MASK >>
		STM32_UCPD_CR_CCENABLE_SHIFT;

	return ((cc_enable >> cc_line) & 0x1);
}

void stm32gx_ucpd1_irq(void)
{
	/* STM32_IRQ_UCPD indicates this is from UCPD1, so port = 0 */
	int port = 0;
	uint32_t sr = STM32_UCPD_SR(port);

	if (sr & (STM32_UCPD_SR_TYPECEVT1 | STM32_UCPD_SR_TYPECEVT2)) {
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
	}
	/* Clear interrupts now that PD events have been set */
	STM32_UCPD_ICR(port) = sr;
}
DECLARE_IRQ(STM32_IRQ_UCPD1, stm32gx_ucpd1_irq, 1);

int stm32gx_ucpd_init(int port)
{
	uint32_t cfgr1_reg;

	/*
	* After exiting reset, stm32gx will have dead battery mode enabled by
	* default which connects Rd to CC1/CC2. This should be disabled when EC
	* is powered up.
	*/
	STM32_PWR_CR3 |= STM32_PWR_CR3_UCPD1_DBDIS;

	/* Ensure that clock to UCPD is enabled */
	STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_UPCD1EN;

	/*
	 * CFGR1 must be written when UCPD peripheral is disabled. Note that
	 * disabling ucpd causes the peripheral to quit any ongoing activity and
	 * sets all ucpd registers back their default values.
	 */
	ucpd_port_enable(port, 0);

	cfgr1_reg = STM32_UCPD_CFGR1_PSC_CLK_VAL(UCPD_PSC_DIV - 1) |
		STM32_UCPD_CFGR1_TRANSWIN_VAL(UCPD_TRANSWIN_CNT - 1) |
		STM32_UCPD_CFGR1_IFRGAP_VAL(UCPD_IFRGAP_CNT - 1) |
		STM32_UCPD_CFGR1_HBITCLKD_VAL(UCPD_HBIT_DIV - 1);
	STM32_UCPD_CFGR1(port) = cfgr1_reg;

	/* Enable ucpd  */
	ucpd_port_enable(port, 1);

	/* Configure CC change interrupts */
	STM32_UCPD_IMR(port) = STM32_UCPD_IMR_TYPECEVT1IE |
		STM32_UCPD_IMR_TYPECEVT2IE;
	STM32_UCPD_ICR(port) = STM32_UCPD_ICR_TYPECEVT1CF |
		STM32_UCPD_ICR_TYPECEVT2CF;

	/* Enable UCPD interrupts */
	task_enable_irq(STM32_IRQ_UCPD1);

	return EC_SUCCESS;
}

int stm32gx_ucpd_release(int port)
{
	ucpd_port_enable(port, 0);

	return EC_SUCCESS;
}

int stm32gx_ucpd_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	int vstate_cc1;
	int vstate_cc2;
	int anamode;
	uint32_t sr;

	/*
	 * cc_voltage_status is determined from vstate_cc bit field in the
	 * status register. The meaning of the value vstate_cc depends on
	 * current value of ANAMODE (src/snk).
	 *
	 * vstate_cc maps directly to cc_state from tcpci spec when ANAMODE = 1,
	 * but needs to be modified slightly for case ANAMODE = 0.
         *
	 * If presenting Rp (source), then need to to a circular shift of
	 * vstate_ccx value:
	 *     vstate_cc | cc_state
	 *     ------------------
	 *        0     ->    1
	 *        1     ->    2
	 *        2     ->    0
	 */

	/* Get vstate_ccx values and power role */
	sr = STM32_UCPD_SR(port);
	/* Get Rp or Rd active */
	anamode = !!(STM32_UCPD_CR(port) & STM32_UCPD_CR_ANAMODE);
	vstate_cc1 = (sr & STM32_UCPD_SR_VSTATE_CC1_MASK) >>
		STM32_UCPD_SR_VSTATE_CC1_SHIFT;
	vstate_cc2 = (sr & STM32_UCPD_SR_VSTATE_CC2_MASK) >>
		STM32_UCPD_SR_VSTATE_CC2_SHIFT;

	/* Do circular shift if port == source */
	if (anamode) {
		if (vstate_cc1 != STM32_UCPD_SR_VSTATE_RA)
			vstate_cc1 += 4;
		if (vstate_cc2 != STM32_UCPD_SR_VSTATE_RA)
			vstate_cc2 += 4;
	} else {
		if (vstate_cc1 != STM32_UCPD_SR_VSTATE_OPEN)
			vstate_cc1 = (vstate_cc1 + 1) % 3;
		if (vstate_cc2 != STM32_UCPD_SR_VSTATE_OPEN)
			vstate_cc2 = (vstate_cc2 + 1) % 3;
	}

	*cc1 = vstate_cc1;
	*cc2 = vstate_cc2;

	return EC_SUCCESS;
}

int stm32gx_ucpd_get_role_control(int port)
{
	int role_control;
	int cc1;
	int cc2;
	int anamode = !!(STM32_UCPD_CR(port) & STM32_UCPD_CR_ANAMODE);
	int anasubmode = (STM32_UCPD_CR(port) & STM32_UCPD_CR_ANASUBMODE_MASK)
		>> STM32_UCPD_CR_ANASUBMODE_SHIFT;

	/*
	 * Role control register is defined as:
	 *     R_cc1 -> b 1:0
	 *     R_cc2 -> b 3:2
	 *     Rp    -> b 5:4
	 *
	 * In TCPCI, CCx is defined as:
	 *    00b -> Ra
	 *    01b -> Rp
	 *    10b -> Rd
	 *    11b -> Open (don't care)
	 *
	 * For ucpd, this information is encoded in ANAMODE and ANASUBMODE
	 * fields as follows:
	 *   ANAMODE            CCx
	 *     0   ->    Rp   -> 1
	 *     1   ->    Rd   -> 2
	 *
	 *   ANASUBMODE:
	 *     00b -> TYPEC_RP_RESERVED (open)
	 *     01b -> TYPEC_RP_USB
	 *     10b -> TYPEC_RP_1A5
	 *     11b -> TYPEC_RP_3A0
	 *
	 *   CCx = ANAMODE + 1, if CCx is enabled
	 *   Rp  = (ANASUBMODE - 1) & 0x3
	 */
	cc1 = ucpd_is_cc_pull_active(port, USBPD_CC_PIN_1) ? anamode + 1 :
		TYPEC_CC_OPEN;
	cc2 = ucpd_is_cc_pull_active(port, USBPD_CC_PIN_2) ? anamode + 1 :
		TYPEC_CC_OPEN;
	role_control = cc1 | (cc2 << 2);
	/* Circular shift anasubmode to convert to Rp range */
	role_control |= (UCPD_ANASUB_TO_RP(anasubmode) << 4);

	return role_control;
}

int stm32gx_ucpd_set_cc(int port, int cc_pull, int rp)
{
	uint32_t cr = STM32_UCPD_CR(port);

	/*
	 * Always set ANASUBMODE to match desired Rp. TCPM layer has a valid
	 * range of 0, 1, or 2. This range maps to 1, 2, or 3 in ucpd for
	 * ANASUBMODE.
	 */
	cr &= ~STM32_UCPD_CR_ANASUBMODE_MASK;
	cr |= STM32_UCPD_CR_ANASUBMODE_VAL(UCPD_RP_TO_ANASUB(rp));

	/* Disconnect both pull from both CC lines by default */
	cr &= ~STM32_UCPD_CR_CCENABLE_MASK;
	/* Set ANAMODE if cc_pull is Rd */
	if (cc_pull == TYPEC_CC_RD) {
		cr |= STM32_UCPD_CR_ANAMODE | STM32_UCPD_CR_CCENABLE_MASK;
	/* Clear ANAMODE if cc_pull is Rp */
	} else if (cc_pull == TYPEC_CC_RP) {
		cr &= ~(STM32_UCPD_CR_ANAMODE);
		cr |= STM32_UCPD_CR_CCENABLE_MASK;
	}

	/* Update pull values */
	STM32_UCPD_CR(port) = cr;

	/* TODO(b/): Should this return error if cc_pull == Ra */
	return EC_SUCCESS;
}

int stm32gx_ucpd_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	/*
	 * Polarity impacts the PHYCCSEL, CCENABLE, and CCxTCDIS fields. This
	 * function is called when polarity is updated at TCPM layer. STM32Gx
	 * only supports POLARITY_CC1 or POLARITY_CC2 and this is stored in the
	 * PHYCCSEL bit in the CR register.
	 */
	if (polarity > POLARITY_CC2)
		return EC_ERROR_UNIMPLEMENTED;

	if (polarity == POLARITY_CC1)
		STM32_UCPD_CR(port) &= ~STM32_UCPD_CR_PHYCCSEL;
	else if (polarity == POLARITY_CC2)
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_PHYCCSEL;

	return EC_SUCCESS;
}

