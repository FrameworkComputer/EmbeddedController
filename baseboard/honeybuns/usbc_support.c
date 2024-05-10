/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USBC functions for RO */

#include "common.h"
#include "console.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "sn5s330.h"
#include "system.h"
#include "timer.h"
#include "ucpd-stm32gx.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

enum usbc_states {
	UNATTACHED_SNK,
	ATTACH_WAIT_SNK,
	ATTACHED_SNK,
};

/* Variables used to manage the simple usbc state machine */
static int usbc_port;
static int usbc_state;
static int usbc_vbus;
static enum tcpc_cc_voltage_status cc1_v;
static enum tcpc_cc_voltage_status cc2_v;

__maybe_unused static __const_data const char *const usbc_state_names[] = {
	[UNATTACHED_SNK] = "Unattached.SNK",
	[ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[ATTACHED_SNK] = "Attached.SNK",
};

static int read_reg(uint8_t port, int reg, int *regval)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr_flags, reg, regval);
}

static int write_reg(uint8_t port, int reg, int regval)
{
	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr_flags, reg, regval);
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
			crec_msleep(1);
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

static void baseboard_ucpd_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
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
}

static int baseboard_rp_is_present(enum tcpc_cc_voltage_status cc1,
				   enum tcpc_cc_voltage_status cc2)
{
	return (cc1 >= TYPEC_CC_VOLT_RP_DEF || cc2 >= TYPEC_CC_VOLT_RP_DEF);
}

static void baseboard_usbc_check_connect(void);
DECLARE_DEFERRED(baseboard_usbc_check_connect);

static void baseboard_usbc_check_connect(void)
{
	enum tcpc_cc_voltage_status cc1;
	enum tcpc_cc_voltage_status cc2;
	int ppc_reg;
	enum usbc_states enter_state = usbc_state;

	/*
	 * In RO, the only usbc related requirement is to enable the stm32g4
	 * USB-EP to be enumerated by the host attached to C0. To prevent D+
	 * being pulled high prior to VBUS presence, the EC uses GPIO_BPWR_DET
	 * to signal the USB hub that VBUS is present. Therefore, we need a
	 * simple usbc state machine to detect an attach (Rp and VBUS) event so
	 * this GPIO signal is properly controlled in RO.
	 *
	 * Note that RO only runs until the RWSIG timer expires and jumps to RW,
	 * and in RW, the full usb-pd stack is initialized and run.
	 */

	/* Get current CC voltage levels */
	baseboard_ucpd_get_cc(usbc_port, &cc1, &cc2);
	/* Update VBUS state */
	if (!read_reg(usbc_port, SN5S330_INT_STATUS_REG3, &ppc_reg))
		usbc_vbus = ppc_reg & SN5S330_VBUS_GOOD;

	switch (usbc_state) {
	case UNATTACHED_SNK:
		/*
		 * Require either CC1 or CC2 to have a valid Rp CC voltage level
		 * to advance to ATTACH_WAIT_SNK.
		 */
		if (baseboard_rp_is_present(cc1, cc2))
			usbc_state = ATTACH_WAIT_SNK;
		break;
	case ATTACH_WAIT_SNK:
		/*
		 * This state handles debounce by ensuring the CC voltages are
		 * the same between two state machine iterations. If this
		 * condition is met, and VBUS is present, then advance to
		 * ATTACHED_SNK and set GPIO_BPWR_DET.
		 *
		 * If Rp voltage is no longer detected, then return to
		 * UNATTACHED_SNK.
		 */
		if (usbc_vbus && cc1 == cc1_v && cc2 == cc2_v) {
			usbc_state = ATTACHED_SNK;
			gpio_set_level(GPIO_BPWR_DET, 1);
		} else if (!baseboard_rp_is_present(cc1, cc2)) {
			usbc_state = UNATTACHED_SNK;
		}
		break;
	case ATTACHED_SNK:
		/*
		 * In this state, only checking for VBUS going away to indicate
		 * a detach event and inform the USB hub via GPIO_BPWR_DET.
		 */
		if (!usbc_vbus) {
			usbc_state = UNATTACHED_SNK;
			gpio_set_level(GPIO_BPWR_DET, 0);
		}
		break;
	}

	/* Save CC voltage for debounce check */
	cc1_v = cc1;
	cc2_v = cc2;

	if (enter_state != usbc_state)
		CPRINTS("%s: cc1 = %d, cc2 = %d vbus = %d",
			usbc_state_names[usbc_state], cc1, cc2, usbc_vbus);

	hook_call_deferred(&baseboard_usbc_check_connect_data,
			   PD_T_TRY_CC_DEBOUNCE);
}

int baseboard_usbc_init(int port)
{
	int rv;

	/* Initialize ucpd and apply Rd to CC lines */
	baseboard_ucpd_apply_rd(port);
	/* Initialize ppc to enable sink path */
	rv = baseboard_ppc_enable_sink_path(port);
	if (rv)
		CPRINTS("ppc init failed!");
	/* Save host port value */
	usbc_port = port;
	/* Start RO usbc attach state machine */
	gpio_set_level(GPIO_BPWR_DET, 0);
	/* Start simple usbc state machine */
	baseboard_usbc_check_connect();

	return rv;
}

#ifdef SECTION_IS_RW
int c1_ps8805_is_vbus_present(int port)
{
	int vbus;

	vbus = tcpm_check_vbus_level(port, VBUS_PRESENT);

	return vbus;
}

int c1_ps8805_is_sourcing_vbus(int port)
{
	int rv;
	int level;

	rv = ps8805_gpio_get_level(port, PS8805_GPIO_1, &level);
	if (rv)
		return 0;

	return level;
}

int c1_ps8805_vbus_source_enable(int port, int enable)
{
	return ps8805_gpio_set_level(port, PS8805_GPIO_1, enable);
}

__override bool usb_ufp_check_usb3_enable(int port)
{
	/* USB3.1 mux should be enabled based on UFP data role */
	return port == USB_PD_PORT_HOST;
}

#ifdef GPIO_USBC_UF_ATTACHED_SRC
static int ppc_ocp_count;

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

	/* Reset OCP event counter for detach */
	if (!level) {
		ppc_ocp_count = 0;

#ifdef GPIO_USB_HUB_OCP_NOTIFY
		/*
		 * In the case of an OCP event on this port, the usb hub should
		 * be notified via a GPIO signal. Following, an OCP, the
		 * attached.src state for the usb3 only port is checked again.
		 * If it's attached, then make sure the OCP notify signal is
		 * reset.
		 */
		gpio_set_level(GPIO_USB_HUB_OCP_NOTIFY, 1);
#endif
	}
}
DECLARE_DEFERRED(baseboard_usb3_manage_vbus);

void baseboard_usb3_check_state(void)
{
	hook_call_deferred(&baseboard_usb3_manage_vbus_data, 0);
}

void baseboard_usbc_usb3_enable_interrupts(int enable)
{
	if (enable) {
		/* Enable VBUS control interrupt for C2 */
		gpio_enable_interrupt(GPIO_USBC_UF_ATTACHED_SRC);
		/* Enable PPC interrupt */
		gpio_enable_interrupt(GPIO_USBC_UF_PPC_INT_ODL);
	} else {
		/* Disable VBUS control interrupt for C2 */
		gpio_disable_interrupt(GPIO_USBC_UF_ATTACHED_SRC);
		/* Disable PPC interrupt */
		gpio_disable_interrupt(GPIO_USBC_UF_PPC_INT_ODL);
	}
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
	/* Reset OCP event counter */
	ppc_ocp_count = 0;

	/* Check state at init time */
	baseboard_usb3_manage_vbus();

	/* Enable attached.src and PPC interrupts */
	baseboard_usbc_usb3_enable_interrupts(1);

	return EC_SUCCESS;
}

static void baseboard_usbc_usb3_handle_interrupt(void)
{
	int port = USB_PD_PORT_USB3;

	/*
	 * SN5S330's /INT pin is level, so process interrupts until it
	 * deasserts if the chip has a dedicated interrupt pin.
	 */
	while (gpio_get_level(GPIO_USBC_UF_PPC_INT_ODL) == 0) {
		int rise = 0;
		int fall = 0;

		read_reg(port, SN5S330_INT_TRIP_RISE_REG1, &rise);
		read_reg(port, SN5S330_INT_TRIP_FALL_REG1, &fall);

		/* Notify the system about the overcurrent event. */
		if (rise & SN5S330_ILIM_PP1_MASK) {
			CPRINTS("usb3_ppc: VBUS OC!");
			gpio_set_level(GPIO_USB_HUB_OCP_NOTIFY, 0);
			if (++ppc_ocp_count < 5)
				hook_call_deferred(
					&baseboard_usb3_manage_vbus_data,
					USB_HUB_OCP_RESET_MSEC);
			else
				CPRINTS("usb3_ppc: VBUS OC limit reached!");
		}

		/* Clear the interrupt sources. */
		write_reg(port, SN5S330_INT_TRIP_RISE_REG1, rise);
		write_reg(port, SN5S330_INT_TRIP_FALL_REG1, fall);

		read_reg(port, SN5S330_INT_TRIP_RISE_REG2, &rise);
		read_reg(port, SN5S330_INT_TRIP_FALL_REG2, &fall);

		/*
		 * VCONN may be latched off due to an overcurrent.  Indicate
		 * when the VCONN overcurrent happens.
		 */
		if (rise & SN5S330_VCONN_ILIM)
			CPRINTS("usb3_ppc: VCONN OC!");

		/*
		 * CC overvoltage event. There is not action to take here, but
		 * log the event.
		 */
		if (rise & SN5S330_CC1_CON || rise & SN5S330_CC2_CON)
			CPRINTS("usb3_ppc: CC OV!");

		/* Clear the interrupt sources. */
		write_reg(port, SN5S330_INT_TRIP_RISE_REG2, rise);
		write_reg(port, SN5S330_INT_TRIP_FALL_REG2, fall);
	}
}
DECLARE_DEFERRED(baseboard_usbc_usb3_handle_interrupt);

void baseboard_usbc_usb3_irq(void)
{
	hook_call_deferred(&baseboard_usbc_usb3_handle_interrupt_data, 0);
}

#endif /* defined(GPIO_USBC_UF_ATTACHED_SRC) */
#endif /* defined(SECTION_IS_RW) */
