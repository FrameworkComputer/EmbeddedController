/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Vconn Power Device API module */

#ifndef __CROS_EC_VPD_API_H
#define __CROS_EC_VPD_API_H

#include "adc.h"
#include "gpio.h"
#include "usb_pd.h"

/*
 * Type C power source charge current limits are identified by their cc
 * voltage (set by selecting the proper Rd resistor). Any voltage below
 * TYPE_C_SRC_DEFAULT_THRESHOLD will not be identified as a type C charger.
 */
#define TYPE_C_SRC_DEFAULT_THRESHOLD    200  /* mV */
#define TYPE_C_SRC_1500_THRESHOLD       660  /* mV */
#define TYPE_C_SRC_3000_THRESHOLD       1230 /* mV */


enum vpd_pin {
	PIN_ADC,
	PIN_CMP,
	PIN_GPO
};

enum vpd_gpo {
	GPO_HZ,
	GPO_HIGH,
	GPO_LOW
};

enum vpd_pwr {
	PWR_VCONN,
	PWR_VBUS,
};

enum vpd_cc {
	CT_OPEN,
	CT_CC1,
	CT_CC2
};

enum vpd_billboard {
	BB_NONE,
	BB_SRC,
	BB_SNK
};

struct mock_pin {
	enum vpd_pin cfg;
	int value;
	int value2;
};

enum vpd_pwr mock_get_vconn_pwr_source(void);
enum vpd_gpo mock_get_ct_rd(void);
enum vpd_gpo mock_get_cc_rp1a5_odh(void);
enum vpd_gpo mock_get_cc_rpusb_odh(void);
enum vpd_gpo mock_get_cc_db_en_od(void);
enum vpd_cc moch_get_ct_cl_sel(void);
int mock_get_mcu_cc_en(void);
enum vpd_billboard mock_get_present_billboard(void);
int mock_get_red_led(void);
int mock_get_green_led(void);
int mock_get_vbus_pass_en(void);
int mock_set_cc_vpdmcu(int v);
void mock_set_host_vbus(int v);
void mock_set_ct_vbus(int v);
void mock_set_vconn(int v);
int mock_get_cfg_cc2_rpusb_odh(void);
int mock_set_cc2_rpusb_odh(int v);
int mock_get_cfg_cc2_rp3a0_rd_l(void);
int mock_set_cc2_rp3a0_rd_l(int v);
int mock_get_cfg_cc1_rpusb_odh(void);
int mock_set_cc1_rpusb_odh(int v);
int mock_get_cfg_cc_vpdmcu(void);
int mock_get_cc_vpdmcu(int v);
enum vpd_pin mock_get_cfg_cc_rp3a0_rd_l(void);
int mock_get_cc_rp3a0_rd_l(void);
int mock_get_cfg_cc1_rp3a0_rd_l(void);
int mock_set_cc1_rp3a0_rd_l(int v);
void mock_set_host_cc_sink_voltage(int v);
void mock_set_host_cc_source_voltage(int v);
int mock_get_ct_cc1_rpusb(void);
int mock_get_ct_cc2_rpusb(void);

/**
 * Set Charge-Through Rp or Rd on CC lines
 *
 * @param pull      Either TYPEC_CC_RP or TYPEC_CC_RD
 * @param rp_value  When pull is RP, set this to
 *                  TYPEC_RP_USB or TYPEC_RP_1A5. Ignored
 *                  for TYPEC_CC_RD
 */
void vpd_ct_set_pull(int pull, int rp_value);

/**
 * Get the status of the Charge-Through CC lines
 *
 * @param cc1  Either TYPEC_CC_VOLT_OPEN,
 *		      TYPEC_CC_VOLT_RA,
 *                    TYPEC_CC_VOLT_RD,
 *                    any other value is considered RP
 * @param cc2  Either TYPEC_CC_VOLT_OPEN,
 *		      TYPEC_CC_VOLT_RA,
 *		      TYPEC_CC_VOLT_RD,
 *		      any other value is considered RP
 */
void vpd_ct_get_cc(int *cc1, int *cc2);

/**
 * Set Host Rp or Rd on CC lines
 *
 * @param pull      Either TYPEC_CC_RP or TYPEC_CC_RD
 * @param rp_value  When pull is RP, set this to
 *                  TYPEC_RP_USB or TYPEC_RP_1A5. Ignored
 *                  for TYPEC_CC_RD
 */
void vpd_host_set_pull(int pull, int rp_value);

/**
 * Get the status of the Host CC line
 *
 * @param cc  Either TYPEC_CC_VOLT_SNK_DEF, TYPEC_CC_VOLT_SNK_1_5,
 *                    TYPEC_CC_VOLT_SNK_3_0, or TYPEC_CC_RD
 */
void vpd_host_get_cc(int *cc);

/**
 * Set RX Enable flag
 *
 * @param en  1 for enable, 0 for disable
 */
void vpd_rx_enable(int en);

/**
 * Configure the cc_vpdmcu pin as ADC, CMP, or GPO
 *
 * @param cfg  PIN_ADC, PIN_CMP, or PIN_GPO
 * @param en   When cfg is PIN_GPO, 1 sets pin high
 *             and 0 sets pin low. Else ignored
 */
void vpd_config_cc_vpdmcu(enum vpd_pin cfg, int en);

/**
 * Configure the cc_rp3a0_rd_l pin as ADC, CMP, or GPO
 *
 * @param cfg  PIN_ADC, PIN_CMP, or PIN_GPO
 * @param en   When cfg is PIN_GPO, 1 sets pin high
 *             and 0 sets pin low. Else ignored
 */
void vpd_config_cc_rp3a0_rd_l(enum vpd_pin cfg, int en);

/**
 * Configure the cc1_rp3a0_rd_l pin as ADC, CMP, or GPO
 *
 * @param cfg  PIN_ADC, PIN_CMP, or PIN_GPO
 * @param en   When cfg is PIN_GPO, 1 sets pin high
 *             and 0 sets pin low. Else ignored
 */
void vpd_config_cc1_rp3a0_rd_l(enum vpd_pin cfg, int en);

/**
 * Configure the cc2_rp3a0_rd_l pin as ADC, CMP, or GPO
 *
 * @param cfg  PIN_ADC, PIN_CMP, or PIN_GPO
 * @param en   When cfg is PIN_GPO, 1 sets pin high
 *             and 0 sets pin low. Else ignored
 */
void vpd_config_cc2_rp3a0_rd_l(enum vpd_pin cfg, int en);

/**
 * Configure the cc1_rpusb_odh pin as ADC, CMP, or GPO
 *
 * @param cfg  PIN_ADC, PIN_CMP, or PIN_GPO
 * @param en   When cfg is PIN_GPO, 1 sets pin high
 *             and 0 sets pin low. Else ignored
 */
void vpd_config_cc1_rpusb_odh(enum vpd_pin cfg, int en);

/**
 * Configure the cc2_rpusb_odh pin as ADC, CMP, or GPO
 *
 * @param cfg  PIN_ADC, PIN_CMP, or PIN_GPO
 * @param en   When cfg is PIN_GPO, 1 sets pin high
 *             and 0 sets pin low. Else ignored
 */
void vpd_config_cc2_rpusb_odh(enum vpd_pin cfg, int en);

/**
 * Configure the cc_db_en_od pin to High-Impedance, low, or high
 *
 * @param val  GPO_HZ, GPO_HIGH, GPO_LOW
 */
void vpd_cc_db_en_od(enum vpd_gpo val);

/**
 * Configure the cc_rpusb_odh pin to High-Impedance, low, or high
 *
 * @param val  GPO_HZ, GPO_HIGH, GPO_LOW
 */
void vpd_cc_rpusb_odh(enum vpd_gpo val);

/**
 * Configure the cc_rp1a5_odh pin to High-Impedance, low, or high
 *
 * @param val  GPO_HZ, GPO_HIGH, GPO_LOW
 */
void vpd_cc_rp1a5_odh(enum vpd_gpo val);

/**
 * Configure the cc1_cc2_db_en_l pin to High-Impedance, low, or high
 *
 * @param val  GPO_HZ, GPO_HIGH, GPO_LOW
 */
void vpd_cc1_cc2_db_en_l(enum vpd_gpo val);

/**
 * Get status of host vbus
 *
 * @return 1 if host vbus is present, else 0
 */
int vpd_is_host_vbus_present(void);

/**
 * Get status of charge-through vbus
 *
 * @return 1 if charge-through vbus is present, else 0
 */
int vpd_is_ct_vbus_present(void);

/**
 * Get status of vconn
 *
 * @return 1 if vconn is present, else 0
 */
int vpd_is_vconn_present(void);

/**
 * Read Host VBUS voltage. Range from 22000mV to 3000mV
 *
 * @return vbus voltage
 */
int vpd_read_host_vbus(void);

/**
 * Read Host CC voltage.
 *
 * @return cc voltage
 */
int vpd_read_cc_host(void);

/**
 * Read voltage on cc_vpdmcu pin
 *
 * @return cc_vpdmcu voltage
 */
int vpd_read_cc_vpdmcu(void);

/**
 * Read charge-through VBUS voltage. Range from 22000mV to 3000mV
 *
 * @return charge-through vbus voltage
 */
int vpd_read_ct_vbus(void);

/**
 * Read VCONN Voltage. Range from 5500mV to 3000mV
 *
 * @return vconn voltage
 */
int vpd_read_vconn(void);

/**
 * Turn ON/OFF Red LED. Should be off when performing power
 * measurements.
 *
 * @param on 0 turns LED off, any other value turns it ON
 */
void vpd_red_led(int on);

/**
 * Turn ON/OFF Green LED. Should be off when performing power
 * measurements.
 *
 * @param on 0 turns LED off, any other value turns it ON
 */
void vpd_green_led(int on);

/**
 * Connects/Disconnects the Host VBUS to the Charge-Through VBUS.
 *
 * @param en 0 disconnectes the VBUS, any other value connects VBUS.
 */
void vpd_vbus_pass_en(int en);

/**
 * Preset Billboard device
 *
 * @param bb  BB_NONE no billboard presented,
 *            BB_SRC source connected but not in charge-through
 *            BB_SNK sink connected
 */
void vpd_present_billboard(enum vpd_billboard bb);

/**
 * Enables the MCU to host cc communication
 *
 * @param en 1 enabled, 0 disabled
 */
void vpd_mcu_cc_en(int en);

/**
 * Selects which supply to power the VPD from
 *
 * @param en PWR_VCONN or PWR_VBUS
 */
void vpd_vconn_pwr_sel_odl(enum vpd_pwr en);

/**
 * Controls if the Charge-Through's CC1, CC2, or neither is
 * connected to Host CC
 *
 * @param sel CT_OPEN neither, CT_CC1 cc1, CT_CC2 cc2
 */
void vpd_ct_cc_sel(enum vpd_cc sel);

#endif /* __CROS_EC_VPD_API_H */
