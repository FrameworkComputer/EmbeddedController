/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "driver/tcpm/tcpm.h"
#include "gpio.h"
#include "registers.h"
#include "vpd_api.h"

/*
 * Polarity based on 'DFP Perspective' (see table 4-10 USB Type-C Cable and
 * Connector Specification Release 1.3)
 *
 * CC1    CC2    STATE             POSITION
 * ----------------------------------------
 * open   open   NC                N/A
 * Rd     open   UFP attached      1
 * open   Rd     UFP attached      2
 * open   Ra     pwr cable no UFP  N/A
 * Ra     open   pwr cable no UFP  N/A
 * Rd     Ra     pwr cable & UFP   1
 * Ra     Rd     pwr cable & UFP   2
 * Rd     Rd     dbg accessory     N/A
 * Ra     Ra     audio accessory   N/A
 *
 * Note, V(Rd) > V(Ra)
 */
#ifndef PD_SRC_RD_THRESHOLD
#define PD_SRC_RD_THRESHOLD PD_SRC_DEF_RD_THRESH_MV
#endif
#ifndef PD_SRC_VNC
#define PD_SRC_VNC PD_SRC_DEF_VNC_MV
#endif

#undef CC_RA
#define CC_RA(cc, sel) (cc < pd_src_rd_threshold[sel])
#undef CC_RD
#define CC_RD(cc, sel) ((cc >= pd_src_rd_threshold[sel]) && (cc < PD_SRC_VNC))

/* (15.8K / (100K + 15.8K)) * 1000 = 136.4 */
#define VBUS_SCALE_FACTOR 136
/* (118K / (100K + 118K)) * 1000 = 541.3 */
#define VCONN_SCALE_FACTOR 541

#define VBUS_DETECT_THRESHOLD 2500 /* mV */
#define VCONN_DETECT_THRESHOLD 2500 /* mV */

#define SCALE(vmeas, sfactor) (((vmeas) * 1000) / (sfactor))

/*
 * Type C power source charge current limits are identified by their cc
 * voltage (set by selecting the proper Rd resistor). Any voltage below
 * TYPE_C_SRC_500_THRESHOLD will not be identified as a type C charger.
 */
#define TYPE_C_SRC_DEFAULT_THRESHOLD 200 /* mV */
#define TYPE_C_SRC_1500_THRESHOLD 660 /* mV */
#define TYPE_C_SRC_3000_THRESHOLD 1230 /* mV */

/* Charge-Through pull up/down enabled */
static int ct_cc_pull;
/* Charge-Through pull up value */
static int ct_cc_rp_value;

/* Charge-Through pull up/down enabled */
static int host_cc_pull;
/* Charge-Through pull up value */
static int host_cc_rp_value;

/* Voltage thresholds for Ra attach in normal SRC mode */
static int pd_src_rd_threshold[TYPEC_RP_RESERVED] = {
	PD_SRC_DEF_RD_THRESH_MV,
	PD_SRC_1_5_RD_THRESH_MV,
	PD_SRC_3_0_RD_THRESH_MV,
};

/* Convert CC voltage to CC status */
static int vpd_cc_voltage_to_status(int cc_volt, int cc_pull)
{
	/* If we have a pull-up, then we are source, check for Rd. */
	if (cc_pull == TYPEC_CC_RP) {
		if (CC_RD(cc_volt, ct_cc_rp_value))
			return TYPEC_CC_RD;
		else if (CC_RA(cc_volt, ct_cc_rp_value))
			return TYPEC_CC_VOLT_RA;
		else
			return TYPEC_CC_VOLT_OPEN;
		/* If we have a pull-down, then we are sink, check for Rp. */
	} else if (cc_pull == TYPEC_CC_RD || cc_pull == TYPEC_CC_RA_RD) {
		if (cc_volt >= TYPE_C_SRC_3000_THRESHOLD)
			return TYPEC_CC_VOLT_RP_3_0;
		else if (cc_volt >= TYPE_C_SRC_1500_THRESHOLD)
			return TYPEC_CC_VOLT_RP_1_5;
		else if (cc_volt >= TYPE_C_SRC_DEFAULT_THRESHOLD)
			return TYPEC_CC_VOLT_RP_DEF;
		else
			return TYPEC_CC_VOLT_OPEN;
	} else {
		/* If we are open, then always return 0 */
		return 0;
	}
}

void vpd_ct_set_pull(int pull, int rp_value)
{
	ct_cc_pull = pull;

	switch (pull) {
	case TYPEC_CC_RP:
		ct_cc_rp_value = rp_value;
		vpd_cc1_cc2_db_en_l(GPO_HIGH);
		switch (rp_value) {
		case TYPEC_RP_USB:
			vpd_config_cc1_rp3a0_rd_l(PIN_ADC, 0);
			vpd_config_cc2_rp3a0_rd_l(PIN_ADC, 0);
			vpd_config_cc1_rpusb_odh(PIN_GPO, 1);
			vpd_config_cc2_rpusb_odh(PIN_GPO, 1);
			break;
		case TYPEC_RP_3A0:
			vpd_config_cc1_rpusb_odh(PIN_ADC, 0);
			vpd_config_cc2_rpusb_odh(PIN_ADC, 0);
			vpd_config_cc1_rp3a0_rd_l(PIN_GPO, 1);
			vpd_config_cc2_rp3a0_rd_l(PIN_GPO, 1);
			break;
		}
		break;
	case TYPEC_CC_RD:
		vpd_config_cc1_rpusb_odh(PIN_ADC, 0);
		vpd_config_cc2_rpusb_odh(PIN_ADC, 0);
		vpd_config_cc1_rp3a0_rd_l(PIN_GPO, 0);
		vpd_config_cc2_rp3a0_rd_l(PIN_GPO, 0);
		vpd_cc1_cc2_db_en_l(GPO_HIGH);
		break;
	case TYPEC_CC_OPEN:
		vpd_cc1_cc2_db_en_l(GPO_HIGH);
		vpd_config_cc1_rpusb_odh(PIN_ADC, 0);
		vpd_config_cc2_rpusb_odh(PIN_ADC, 0);
		vpd_config_cc1_rp3a0_rd_l(PIN_ADC, 0);
		vpd_config_cc2_rp3a0_rd_l(PIN_ADC, 0);
		break;
	}
}

void vpd_ct_get_cc(int *cc1, int *cc2)
{
	int cc1_v = 0;
	int cc2_v = 0;

	switch (ct_cc_pull) {
	case TYPEC_CC_RP:
		switch (ct_cc_rp_value) {
		case TYPEC_RP_USB:
			cc1_v = adc_read_channel(ADC_CC1_RP3A0_RD_L);
			cc2_v = adc_read_channel(ADC_CC2_RP3A0_RD_L);
			break;
		case TYPEC_RP_3A0:
			cc1_v = adc_read_channel(ADC_CC1_RPUSB_ODH);
			cc2_v = adc_read_channel(ADC_CC2_RPUSB_ODH);
			break;
		}
		break;
	case TYPEC_CC_RD:
		cc1_v = adc_read_channel(ADC_CC1_RPUSB_ODH);
		cc2_v = adc_read_channel(ADC_CC2_RPUSB_ODH);
		break;
	case TYPEC_CC_OPEN:
		*cc1 = 0;
		*cc2 = 0;
		return;
	}

	*cc1 = vpd_cc_voltage_to_status(cc1_v, ct_cc_pull);
	*cc2 = vpd_cc_voltage_to_status(cc2_v, ct_cc_pull);
}

void vpd_host_set_pull(int pull, int rp_value)
{
	host_cc_pull = pull;

	switch (pull) {
	case TYPEC_CC_RP:
		vpd_cc_db_en_od(GPO_LOW);
		host_cc_rp_value = rp_value;
		switch (rp_value) {
		case TYPEC_RP_USB:
			vpd_config_cc_rp3a0_rd_l(PIN_CMP, 0);
			vpd_cc_rpusb_odh(GPO_HIGH);
			break;
		case TYPEC_RP_3A0:
			vpd_cc_rpusb_odh(GPO_HZ);
			vpd_config_cc_rp3a0_rd_l(PIN_GPO, 1);
			break;
		}
		break;
	case TYPEC_CC_RD:
		vpd_cc_rpusb_odh(GPO_HZ);
		vpd_cc_db_en_od(GPO_LOW);
		vpd_config_cc_rp3a0_rd_l(PIN_GPO, 0);
		break;
	case TYPEC_CC_RA_RD:
		vpd_cc_rpusb_odh(GPO_HZ);
		vpd_config_cc_rp3a0_rd_l(PIN_CMP, 0);

		/*
		 * RA is connected to VCONN
		 * RD is connected to CC
		 */
		vpd_cc_db_en_od(GPO_HIGH);
		break;
	case TYPEC_CC_OPEN:
		vpd_cc_rpusb_odh(GPO_HZ);
		vpd_config_cc_rp3a0_rd_l(PIN_CMP, 0);
		vpd_cc_db_en_od(GPO_LOW);
		break;
	}
}

void vpd_host_get_cc(int *cc)
{
	*cc = vpd_cc_voltage_to_status(adc_read_channel(ADC_CC_VPDMCU),
				       host_cc_pull);
}

void vpd_rx_enable(int en)
{
	tcpm_set_rx_enable(0, en);
}

/*
 * PA2: Configure as COMP2_INM6 or GPO
 */
void vpd_config_cc_rp3a0_rd_l(enum vpd_pin cfg, int en)
{
	if (cfg == PIN_GPO) {
		/* Set output value in register */
		gpio_set_level(GPIO_CC_RP3A0_RD_L, en ? 1 : 0);

		/* Disable Analog mode and Enable GPO */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) & ~(3 << (2 * 2))) /* PA2
									disable
									ADC */
			| (1 << (2 * 2)); /* Set as GPO */
	} else {
		/* Set PA2 pin to ANALOG function */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) | (3 << (2 * 2))); /* PA2 in
									ANALOG
									mode */

		/* Set PA3 pin to ANALOG function */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) | (3 << (2 * 3))); /* PA3 in
									ANALOG
									mode */

		/* Disable Window Mode. Select PA3 */
		STM32_COMP_CSR &= ~STM32_COMP_WNDWEN;

		/* No output selection. We will use Interrupt */
		STM32_COMP_CSR &= ~STM32_COMP_CMP2OUTSEL_NONE;

		/* Not inverting */
		STM32_COMP_CSR &= ~STM32_COMP_CMP2POL;

		/* Select COMP2_INM6 (PA2) */
		STM32_COMP_CSR |= STM32_COMP_CMP2INSEL_INM6;

		/* COMP Enable */
		STM32_COMP_CSR |= STM32_COMP_CMP2EN;
	}
}

/*
 * PA4: Configure as ADC, CMP, or GPO
 */
void vpd_config_cc1_rp3a0_rd_l(enum vpd_pin cfg, int en)
{
	if (cfg == PIN_GPO) {
		/* Default high. Enable cc1 Rp3A0 pullup */
		gpio_set_level(GPIO_CC1_RP3A0_RD_L, en ? 1 : 0);

		/* Disable Analog mode and Enable GPO */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) & ~(3 << (2 * 4))) /* PA4
									disable
									ADC */
			| (1 << (2 * 4)); /* Set as GPO */
	}

	if (cfg == PIN_ADC || cfg == PIN_CMP) {
		/* Disable COMP2 */
		STM32_COMP_CSR &= ~STM32_COMP_CMP2EN;

		/* Set PA4 pin to Analog mode */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) | (3 << (2 * 4))); /* PA4 in
									ANALOG
									mode */

		if (cfg == PIN_CMP) {
			/* Set PA3 pin to ANALOG function */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A) |
						    (3 << (2 * 3))); /* PA3 in
									ANALOG
									mode */

			/* Disable Window Mode. Select PA3*/
			STM32_COMP_CSR &= ~STM32_COMP_WNDWEN;

			/* No output selection. We will use Interrupt */
			STM32_COMP_CSR &= ~STM32_COMP_CMP2OUTSEL_NONE;

			/* Select COMP2_INM4 (PA4) */
			STM32_COMP_CSR |= STM32_COMP_CMP2INSEL_INM4;

			/* COMP2 Enable */
			STM32_COMP_CSR |= STM32_COMP_CMP2EN;
		}
	}
}

/*
 * PA5: Configure as ADC, COMP, or GPO
 */
void vpd_config_cc2_rp3a0_rd_l(enum vpd_pin cfg, int en)
{
	if (cfg == PIN_GPO) {
		/* Set output value in register */
		gpio_set_level(GPIO_CC2_RP3A0_RD_L, en ? 1 : 0);

		/* Disable Analog mode and Enable GPO */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) & ~(3 << (2 * 5))) /* PA5
									disable
									ADC */
			| (1 << (2 * 5)); /* Set as GPO */
	}

	if (cfg == PIN_ADC || cfg == PIN_CMP) {
		/* Disable COMP2 */
		STM32_COMP_CSR &= ~STM32_COMP_CMP2EN;

		/* Set PA5 pin to ANALOG function */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) | (3 << (2 * 5))); /* PA5 in
									ANALOG
									mode */

		if (cfg == PIN_CMP) {
			/* Set PA3 pin to ANALOG function */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A) |
						    (3 << (2 * 3))); /* PA3 in
									ANALOG
									mode */

			/* Disable Window Mode. */
			STM32_COMP_CSR &= ~STM32_COMP_WNDWEN;

			/* No output selection. We will use Interrupt */
			STM32_COMP_CSR &= ~STM32_COMP_CMP2OUTSEL_NONE;

			/* Select COMP2_INM5 (PA5) */
			STM32_COMP_CSR |= STM32_COMP_CMP2INSEL_INM5;

			/* COMP2 Enable */
			STM32_COMP_CSR |= STM32_COMP_CMP2EN;
		}
	}
}

/*
 * PB0: Configure as ADC or GPO
 */
void vpd_config_cc1_rpusb_odh(enum vpd_pin cfg, int en)
{
	if (cfg == PIN_GPO) {
		/* Set output value in register */
		gpio_set_level(GPIO_CC1_RPUSB_ODH, en ? 1 : 0);

		/* Disable Analog mode and Enable GPO */
		STM32_GPIO_MODER(GPIO_B) =
			(STM32_GPIO_MODER(GPIO_B) & ~(3 << (2 * 0))) /* PB0
									disable
									ADC */
			| (1 << (2 * 0)); /* Set as GPO */
	} else {
		/* Enable Analog mode */
		STM32_GPIO_MODER(GPIO_B) =
			(STM32_GPIO_MODER(GPIO_B) | (3 << (2 * 0))); /* PB0 in
									ANALOG
									mode */
	}
}

/*
 * PB1: Configure as ADC or GPO
 */
void vpd_config_cc2_rpusb_odh(enum vpd_pin cfg, int en)
{
	if (cfg == PIN_GPO) {
		/* Set output value in register */
		gpio_set_level(GPIO_CC2_RPUSB_ODH, en ? 1 : 0);

		/* Disable Analog mode and Enable GPO */
		STM32_GPIO_MODER(GPIO_B) =
			(STM32_GPIO_MODER(GPIO_B) & ~(3 << (2 * 1))) /* PB1
									disable
									ADC */
			| (1 << (2 * 1)); /* Set as GPO */
	} else {
		/* Enable Analog mode */
		STM32_GPIO_MODER(GPIO_B) =
			(STM32_GPIO_MODER(GPIO_B) | (3 << (2 * 1))); /* PB1 in
									ANALOG
									mode */
	}
}

inline int vpd_read_cc_vpdmcu(void)
{
	return adc_read_channel(ADC_CC_VPDMCU);
}

inline int vpd_read_host_vbus(void)
{
	return SCALE(adc_read_channel(ADC_HOST_VBUS_VSENSE), VBUS_SCALE_FACTOR);
}

inline int vpd_read_ct_vbus(void)
{
	return SCALE(adc_read_channel(ADC_CHARGE_VBUS_VSENSE),
		     VBUS_SCALE_FACTOR);
}

inline int vpd_read_vconn(void)
{
	return SCALE(adc_read_channel(ADC_VCONN_VSENSE), VCONN_SCALE_FACTOR);
}

inline int vpd_is_host_vbus_present(void)
{
	return (vpd_read_host_vbus() >= VBUS_DETECT_THRESHOLD);
}

inline int vpd_is_ct_vbus_present(void)
{
	return (vpd_read_ct_vbus() >= VBUS_DETECT_THRESHOLD);
}

inline int vpd_is_vconn_present(void)
{
	return (vpd_read_vconn() >= VCONN_DETECT_THRESHOLD);
}

inline int vpd_read_rdconnect_ref(void)
{
	return adc_read_channel(ADC_RDCONNECT_REF);
}

void vpd_red_led(int on)
{
	gpio_set_level(GPIO_DEBUG_LED_R_L, (on) ? 0 : 1);
}

void vpd_green_led(int on)
{
	gpio_set_level(GPIO_DEBUG_LED_G_L, (on) ? 0 : 1);
}

void vpd_vbus_pass_en(int en)
{
	gpio_set_level(GPIO_VBUS_PASS_EN, (en) ? 1 : 0);
}

void vpd_present_billboard(enum vpd_billboard bb)
{
	switch (bb) {
	case BB_NONE:
		gpio_set_level(GPIO_PRESENT_BILLBOARD, 0);
		gpio_set_flags(GPIO_PRESENT_BILLBOARD, GPIO_OUTPUT);
		break;
	case BB_SRC:
		gpio_set_flags(GPIO_PRESENT_BILLBOARD, GPIO_INPUT);
		/* Enable Pull-up on PA8 */
		STM32_GPIO_PUPDR(GPIO_A) |= (1 << (2 * 8));
		break;
	case BB_SNK:
		gpio_set_level(GPIO_PRESENT_BILLBOARD, 1);
		gpio_set_flags(GPIO_PRESENT_BILLBOARD, GPIO_OUTPUT);
		break;
	}
}

void vpd_mcu_cc_en(int en)
{
	gpio_set_level(GPIO_VPDMCU_CC_EN, (en) ? 1 : 0);
}

void vpd_ct_cc_sel(enum vpd_cc sel)
{
	switch (sel) {
	case CT_OPEN:
		gpio_set_level(GPIO_CC1_SEL, 0);
		gpio_set_level(GPIO_CC2_SEL, 0);
		break;
	case CT_CC1:
		gpio_set_level(GPIO_CC2_SEL, 0);
		gpio_set_level(GPIO_CC1_SEL, 1);
		break;
	case CT_CC2:
		gpio_set_level(GPIO_CC1_SEL, 0);
		gpio_set_level(GPIO_CC2_SEL, 1);
		break;
	}
}

/* Set as GPO High, GPO Low, or High-Z */
void vpd_cc_db_en_od(enum vpd_gpo val)
{
	if (val == GPO_HZ) {
		gpio_set_flags(GPIO_CC_DB_EN_OD, GPIO_INPUT);
	} else {
		if (val == GPO_HIGH)
			gpio_set_level(GPIO_CC_DB_EN_OD, 1);
		else
			gpio_set_level(GPIO_CC_DB_EN_OD, 0);

		gpio_set_flags(GPIO_CC_DB_EN_OD, GPIO_OUTPUT);
	}
}

void vpd_cc_rpusb_odh(enum vpd_gpo val)
{
	if (val == GPO_HZ) {
		gpio_set_flags(GPIO_CC_RPUSB_ODH, GPIO_INPUT);
	} else {
		gpio_set_level(GPIO_CC_RPUSB_ODH, (val == GPO_HIGH) ? 1 : 0);
		gpio_set_flags(GPIO_CC_RPUSB_ODH, GPIO_OUTPUT);
	}
}

void vpd_cc1_cc2_db_en_l(enum vpd_gpo val)
{
	if (val == GPO_HZ) {
		gpio_set_flags(GPIO_CC1_CC2_DB_EN_L, GPIO_INPUT);
	} else {
		gpio_set_level(GPIO_CC1_CC2_DB_EN_L, (val == GPO_HIGH) ? 1 : 0);
		gpio_set_flags(GPIO_CC1_CC2_DB_EN_L, GPIO_OUTPUT);
	}
}

void vpd_vconn_pwr_sel_odl(enum vpd_pwr en)
{
	gpio_set_level(GPIO_VCONN_PWR_SEL_ODL, (en == PWR_VBUS) ? 1 : 0);
}
