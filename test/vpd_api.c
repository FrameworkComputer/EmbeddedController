/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"
#include "vpd_api.h"
#include "driver/tcpm/tcpm.h"
#include "console.h"
/*
 * Polarity based on 'DFP Perspective' (see table USB Type-C Cable and Connector
 * Specification)
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

#ifndef CC_RA
#define CC_RA(port, cc, sel)  (cc < pd_src_rd_threshold[ct_cc_rp_value])
#endif
#define CC_RD(cc) ((cc >= PD_SRC_RD_THRESHOLD) && (cc < PD_SRC_VNC))
#ifndef CC_NC
#define CC_NC(port, cc, sel)  (cc >= PD_SRC_VNC)
#endif

/*
 * Polarity based on 'UFP Perspective'.
 *
 * CC1    CC2    STATE             POSITION
 * ----------------------------------------
 * open   open   NC                N/A
 * Rp     open   DFP attached      1
 * open   Rp     DFP attached      2
 * Rp     Rp     Accessory attached N/A
 */
#ifndef PD_SNK_VA
#define PD_SNK_VA PD_SNK_VA_MV
#endif

#define CC_RP(cc)  (cc >= PD_SNK_VA)

/* Mock Board State */
static enum vpd_pwr mock_vconn_pwr_sel_odl;
static enum vpd_gpo mock_cc1_cc2_rd_l;
static enum vpd_gpo mock_cc_db_en_od;
static enum vpd_gpo mock_cc_rpusb_odh;
static enum vpd_cc mock_ct_cl_sel;
static int mock_mcu_cc_en;
static enum vpd_billboard mock_present_billboard;
static int mock_red_led;
static int mock_green_led;
static int mock_vbus_pass_en;

static int mock_read_host_vbus;
static int mock_read_ct_vbus;
static int mock_read_vconn;

static struct mock_pin mock_cc2_rpusb_odh;
static struct mock_pin mock_cc2_rp3a0_rd_l;
static struct mock_pin mock_cc1_rpusb_odh;
static struct mock_pin mock_cc1_rp3a0_rd_l;
static struct mock_pin mock_cc_vpdmcu;
static struct mock_pin mock_cc_rp3a0_rd_l;

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

enum vpd_pwr mock_get_vconn_pwr_source(void)
{
	return mock_vconn_pwr_sel_odl;
}

int mock_get_ct_cc1_rpusb(void)
{
	return mock_cc1_rpusb_odh.value;
}

int mock_get_ct_cc2_rpusb(void)
{
	return mock_cc2_rpusb_odh.value;
}

enum vpd_gpo mock_get_ct_rd(void)
{
	return mock_cc1_cc2_rd_l;
}

enum vpd_gpo mock_get_cc_rpusb_odh(void)
{
	return mock_cc_rpusb_odh;
}

enum vpd_gpo mock_get_cc_db_en_od(void)
{
	return mock_cc_db_en_od;
}

enum vpd_cc moch_get_ct_cl_sel(void)
{
	return mock_ct_cl_sel;
}

int mock_get_mcu_cc_en(void)
{
	return mock_mcu_cc_en;
}

enum vpd_billboard mock_get_present_billboard(void)
{
	return mock_present_billboard;
}

int mock_get_red_led(void)
{
	return mock_red_led;
}

int mock_get_green_led(void)
{
	return mock_green_led;
}

int mock_get_vbus_pass_en(void)
{
	return mock_vbus_pass_en;
}

void mock_set_host_cc_sink_voltage(int v)
{
	mock_cc_vpdmcu.value = v;
}

void mock_set_host_cc_source_voltage(int v)
{
	mock_cc_vpdmcu.value2 = v;
}

void mock_set_host_vbus(int v)
{
	mock_read_host_vbus = v;
}

void mock_set_ct_vbus(int v)
{
	mock_read_ct_vbus = v;
}

void mock_set_vconn(int v)
{
	mock_read_vconn = v;
}

int mock_get_cfg_cc2_rpusb_odh(void)
{
	return mock_cc2_rpusb_odh.cfg;
}

int mock_set_cc2_rpusb_odh(int v)
{
	if (mock_cc2_rpusb_odh.cfg == PIN_ADC) {
		mock_cc2_rpusb_odh.value = v;
		return 1;
	}
	return 0;
}

int mock_get_cfg_cc2_rp3a0_rd_l(void)
{
	return mock_cc2_rp3a0_rd_l.cfg;
}

int mock_set_cc2_rp3a0_rd_l(int v)
{
	if (mock_cc2_rp3a0_rd_l.cfg == PIN_ADC) {
		mock_cc2_rp3a0_rd_l.value = v;
		return 1;
	}

	return 0;
}

int mock_get_cc1_rpusb_odh(void)
{
	return mock_cc1_rpusb_odh.cfg;
}

int mock_set_cc1_rpusb_odh(int v)
{
	if (mock_cc1_rpusb_odh.cfg == PIN_ADC) {
		mock_cc1_rpusb_odh.value = v;
		return 1;
	}

	return 0;
}

int mock_get_cfg_cc_vpdmcu(void)
{
	return mock_cc_vpdmcu.cfg;
}

enum vpd_pin mock_get_cfg_cc_rp3a0_rd_l(void)
{
	return mock_cc_rp3a0_rd_l.cfg;
}

int mock_get_cc_rp3a0_rd_l(void)
{
	return mock_cc_rp3a0_rd_l.value;
}

int mock_get_cfg_cc1_rp3a0_rd_l(void)
{
	return mock_cc1_rp3a0_rd_l.cfg;
}

int mock_set_cc1_rp3a0_rd_l(int v)
{
	if (mock_cc1_rp3a0_rd_l.cfg == PIN_ADC) {
		mock_cc1_rp3a0_rd_l.value = v;
		return 1;
	}

	return 0;
}

/* Convert CC voltage to CC status */
static int vpd_cc_voltage_to_status(int cc_volt, int cc_pull)
{
	/* If we have a pull-up, then we are source, check for Rd. */
	if (cc_pull == TYPEC_CC_RP) {
		if (CC_NC(0, cc_volt, 0))
			return TYPEC_CC_VOLT_OPEN;
		else if (CC_RA(0, cc_volt, 0))
			return TYPEC_CC_VOLT_RA;
		else
			return TYPEC_CC_VOLT_RD;
	/* If we have a pull-down, then we are sink, check for Rp. */
	} else if (cc_pull == TYPEC_CC_RD || cc_pull == TYPEC_CC_RA_RD) {
		if (cc_volt >= TYPE_C_SRC_3000_THRESHOLD)
			return TYPEC_CC_VOLT_RP_3_0;
		else if (cc_volt >= TYPE_C_SRC_1500_THRESHOLD)
			return TYPEC_CC_VOLT_RP_1_5;
		else if (CC_RP(cc_volt))
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
		vpd_config_cc1_rp3a0_rd_l(PIN_ADC, 0);
		vpd_config_cc2_rp3a0_rd_l(PIN_ADC, 0);
		vpd_cc1_cc2_db_en_l(GPO_LOW);
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
	int cc1_v;
	int cc2_v;

	switch (ct_cc_pull) {
	case TYPEC_CC_RP:
		switch (ct_cc_rp_value) {
		case TYPEC_RP_USB:
			cc1_v = mock_cc1_rp3a0_rd_l.value;
			cc2_v = mock_cc2_rp3a0_rd_l.value;
			break;
		case TYPEC_RP_3A0:
			cc1_v = mock_cc1_rpusb_odh.value;
			cc2_v = mock_cc2_rpusb_odh.value;
			break;
		}

		if (!cc1_v && !cc2_v) {
			cc1_v = PD_SRC_VNC;
			cc2_v = PD_SRC_VNC;
		}
		break;
	case TYPEC_CC_RD:
		cc1_v = mock_cc1_rpusb_odh.value;
		cc2_v = mock_cc2_rpusb_odh.value;
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
		vpd_config_cc_rp3a0_rd_l(PIN_GPO, 0);

		/*
		 * RA is connected to VCONN
		 * RD is connected to CC
		 */
		vpd_cc_db_en_od(GPO_HZ);
		break;
	case TYPEC_CC_OPEN:
		vpd_cc_rpusb_odh(GPO_HZ);
		vpd_config_cc_rp3a0_rd_l(PIN_CMP, 0);
		vpd_cc_db_en_od(GPO_LOW);

		/*
		 * Do nothing. CC is open on entry to this function
		 */
		break;
	}
}

void vpd_host_get_cc(int *cc)
{
	int v;

	if (host_cc_pull == TYPEC_CC_OPEN) {
		*cc = 0;
		return;
	} else if (host_cc_pull == TYPEC_CC_RP) {
		v = mock_cc_vpdmcu.value;
	} else {
		v = mock_cc_vpdmcu.value2;
	}

	*cc = vpd_cc_voltage_to_status(v, host_cc_pull);
}

void vpd_rx_enable(int en)
{
	if (en) {
		mock_ct_cl_sel = 0;
		mock_mcu_cc_en = 1;
	}

	tcpm_set_polarity(0, 0);
	tcpm_set_rx_enable(0, en);
}

/*
 * PA1: Configure as ADC, CMP, or GPO
 */
void vpd_config_cc_vpdmcu(enum vpd_pin cfg, int en)
{
	mock_cc_vpdmcu.cfg = cfg;

	if (cfg == PIN_GPO)
		mock_cc_vpdmcu.value = en ? 1 : 0;
}

/*
 * PA2: Configure as COMP2_INM6 or GPO
 */
void vpd_config_cc_rp3a0_rd_l(enum vpd_pin cfg, int en)
{
	mock_cc_rp3a0_rd_l.cfg = cfg;

	if (cfg == PIN_GPO)
		mock_cc_rp3a0_rd_l.value = en ? 1 : 0;
}

/*
 * PA4: Configure as ADC, CMP, or GPO
 */
void vpd_config_cc1_rp3a0_rd_l(enum vpd_pin cfg, int en)
{
	mock_cc1_rp3a0_rd_l.cfg = cfg;

	if (cfg == PIN_GPO)
		mock_cc1_rp3a0_rd_l.value = en ? 1 : 0;
}

/*
 * PA5: Configure as ADC, COMP, or GPO
 */
void vpd_config_cc2_rp3a0_rd_l(enum vpd_pin cfg, int en)
{
	mock_cc2_rp3a0_rd_l.cfg = cfg;

	if (cfg == PIN_GPO)
		mock_cc2_rp3a0_rd_l.value = en ? 1 : 0;
}

/*
 * PB0: Configure as ADC or GPO
 */
void vpd_config_cc1_rpusb_odh(enum vpd_pin cfg, int en)
{
	mock_cc1_rpusb_odh.cfg = cfg;

	if (cfg == PIN_GPO)
		mock_cc1_rpusb_odh.value = en ? 1 : 0;
}

/*
 * PB1: Configure as ADC or GPO
 */
void vpd_config_cc2_rpusb_odh(enum vpd_pin cfg, int en)
{
	mock_cc2_rpusb_odh.cfg = cfg;

	if (cfg == PIN_GPO)
		mock_cc2_rpusb_odh.value = en ? 1 : 0;
}

int vpd_read_host_vbus(void)
{
	return mock_read_host_vbus;
}

int vpd_read_ct_vbus(void)
{
	return mock_read_ct_vbus;
}

int vpd_read_vconn(void)
{
	return mock_read_vconn;
}

int vpd_is_host_vbus_present(void)
{
	return (vpd_read_host_vbus() >= PD_SNK_VA);
}

int vpd_is_ct_vbus_present(void)
{
	return (vpd_read_ct_vbus() >= PD_SNK_VA);
}

int vpd_is_vconn_present(void)
{
	return (vpd_read_vconn() >= PD_SNK_VA);
}

int vpd_read_rdconnect_ref(void)
{
	return 200; /* 200 mV */
}

void vpd_red_led(int on)
{
	mock_red_led = on ? 0 : 1;
}

void vpd_green_led(int on)
{
	mock_green_led = on ? 0 : 1;
}

void vpd_vbus_pass_en(int en)
{
	mock_vbus_pass_en = en ? 1 : 0;
}

void vpd_present_billboard(enum vpd_billboard bb)
{
	mock_present_billboard = bb;
}

void vpd_mcu_cc_en(int en)
{
	mock_mcu_cc_en = en ? 1 : 0;
}

void vpd_ct_cc_sel(enum vpd_cc sel)
{
	mock_ct_cl_sel = sel;
}

/* Set as GPO High, GPO Low, or High-Z */
void vpd_cc_db_en_od(enum vpd_gpo val)
{
	mock_cc_db_en_od = val;
}

void vpd_cc_rpusb_odh(enum vpd_gpo val)
{
	mock_cc_rpusb_odh = val;
}

void vpd_cc1_cc2_db_en_l(enum vpd_gpo val)
{
	mock_cc1_cc2_rd_l = val;
}

void vpd_vconn_pwr_sel_odl(enum vpd_pwr en)
{
	mock_vconn_pwr_sel_odl = en;
}
