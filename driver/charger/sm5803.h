/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Mitus SM5803 Buck-Boost Charger
 */

#ifndef __CROS_EC_SM5803_H
#define __CROS_EC_SM5803_H

#include "common.h"
#include "usb_pd_tcpm.h"

/* Note: configure charger struct with CHARGER_FLAGS */
#define SM5803_ADDR_MAIN_FLAGS 0x30
#define SM5803_ADDR_MEAS_FLAGS 0x31
#define SM5803_ADDR_CHARGER_FLAGS 0x32
#define SM5803_ADDR_TEST_FLAGS 0x37

/* Main registers (address 0x30) */

#define SM5803_REG_CHIP_ID 0x00

#define SM5803_REG_STATUS1 0x01
#define SM5803_STATUS1_VSYS_OK BIT(0)
#define SM5803_STATUS1_VPWR_OK BIT(1)
#define SM5803_STATUS1_VBUS_UVL BIT(3)
#define SM5803_STATUS1_VBUS_SHORT BIT(4)
#define SM5803_STATUS1_VBUS_OVH BIT(5)
#define SM5803_STATUS1_CHG_DET BIT(6)
#define SM5803_STATUS1_BAT_DET BIT(7)

#define SM5803_REG_STATUS2 0x02
#define SM5803_STATUS2_BAT_DET_FG BIT(1)
#define SM5803_STATUS2_VBAT_SHORT BIT(0)

#define SM5803_REG_INT1_REQ 0x05
#define SM5803_REG_INT1_EN 0x0A
#define SM5803_INT1_VBUS_PWR_HWSAFE_LIMIT BIT(0)
#define SM5803_INT1_CHG BIT(2)
#define SM5803_INT1_BAT BIT(3)
#define SM5803_INT1_CLS_OC BIT(4)
#define SM5803_INT1_SLV_DET BIT(5)
#define SM5803_INT1_SWL_DISCH BIT(6)
#define SM5803_INT1_PREREG BIT(7)

#define SM5803_REG_INT2_REQ 0x06
#define SM5803_REG_INT2_EN 0x0B
#define SM5803_INT2_VBATSNSP BIT(0)
#define SM5803_INT2_IBAT_DISCHG BIT(1)
#define SM5803_INT2_IBAT_CHG BIT(2)
#define SM5803_INT2_IBUS BIT(3)
#define SM5803_INT2_VBUS BIT(4)
#define SM5803_INT2_VCHGPWR BIT(5)
#define SM5803_INT2_VSYS BIT(6)
#define SM5803_INT2_TINT BIT(7)

#define SM5803_REG_INT3_REQ 0x07
#define SM5803_REG_INT3_EN 0x0C
#define SM5803_INT3_GPADC0 BIT(0)
#define SM5803_INT3_BFET_PWR_LIMIT BIT(1)
#define SM5803_INT3_BFET_PWR_HWSAFE_LIMIT BIT(2)
#define SM5803_INT3_SPARE BIT(3)
#define SM5803_INT3_VBUS_PWR_LIMIT BIT(4)
#define SM5803_INT3_IBAT BIT(5)

#define SM5803_REG_INT4_REQ 0x08
#define SM5803_REG_INT4_EN 0x0D
#define SM5803_INT4_CHG_FAIL BIT(0)
#define SM5803_INT4_CHG_DONE BIT(1)
#define SM5803_INT4_CHG_START BIT(2)
#define SM5803_INT4_SLP_EXIT BIT(3)
#define SM5803_INT4_OTG_FAIL BIT(4)
#define SM5803_INT4_CHG_ILIM BIT(5)
#define SM5803_INT4_IBAT_CC BIT(6)
#define SM5803_INT4_CC BIT(7)

#define SM5803_REG_MISC_CONFIG 0x15
#define SM5803_MISC_INV_INT BIT(0)
#define SM5803_INT_CLEAR_MODE BIT(1)
#define SM5803_INT_MASK_MODE BIT(2)

#define SM5803_REG_PLATFORM 0x18
#define SM5803_PLATFORM_ID GENMASK(4, 0)

#define SM5803_REG_REFERENCE 0x20
#define SM5803_REFERENCE_LDO3P3_PGOOD BIT(4)
#define SM5803_REFERENCE_LDO5_PGOOD BIT(5)

#define SM5803_REG_CLOCK_SEL 0x2A
#define SM5803_CLOCK_SEL_LOW BIT(0)

#define SM5803_REG_GPIO0_CTRL 0x30
#define SM5803_GPIO0_VAL BIT(0)
#define SM5803_GPIO0_MODE_MASK GENMASK(2, 1)
#define SM5803_GPIO0_OPEN_DRAIN_EN BIT(6)
#define SM5803_CHG_DET_OPEN_DRAIN_EN BIT(7)

#define SM5803_REG_VBATSNSP_MEAS_MSB 0x40
#define SM5803_REG_VBATSNSP_MEAS_LSB 0x41

enum sm5803_gpio0_modes {
	GPIO0_MODE_PROCHOT,
	GPIO0_MODE_OUTPUT,
	GPIO0_MODE_INPUT
};

#define SM5803_REG_BFET_PWR_MAX_TH 0x35
#define SM5803_REG_BFET_PWR_HWSAFE_MAX_TH 0x36

#define SM5803_REG_PORTS_CTRL 0x40
#define SM5803_PORTS_VBUS_DISCH BIT(0)
#define SM5803_PORTS_VBUS_PULLDOWN BIT(1)
#define SM5803_PORTS_VBUS_SNS_DISCH BIT(2)
#define SM5803_PORTS_VBUS_SNS_PULLDOWN BIT(3)

/* ADC Registers (address 0x31) */

/*
 * Note: Some register bits must be enabled for the DC-DC converter to properly
 * handle transitions.
 */
#define SM5803_REG_GPADC_CONFIG1 0x01
#define SM5803_GPADCC1_VBATSNSP_EN BIT(0)
#define SM5803_GPADCC1_IBAT_DIS_EN BIT(1)
#define SM5803_GPADCC1_IBAT_CHG_EN BIT(2)
#define SM5803_GPADCC1_IBUS_EN BIT(3)
#define SM5803_GPADCC1_VBUS_EN BIT(4)
#define SM5803_GPADCC1_VCHGPWR_EN BIT(5) /* NOTE: DO NOT CLEAR */
#define SM5803_GPADCC1_VSYS_EN BIT(6) /* NOTE: DO NOT CLEAR */
#define SM5803_GPADCC1_TINT_EN BIT(7)

/*
 * Default value for GPADCC1, set at initialization: the normal operating state.
 *
 * IBAT_CHG is enabled in order to measure battery current and calculate system
 * resistance.
 */
#define SM5803_GPADCC1_DEFAULT_ENABLE                              \
	(SM5803_GPADCC1_TINT_EN | SM5803_GPADCC1_VSYS_EN |         \
	 SM5803_GPADCC1_VCHGPWR_EN | SM5803_GPADCC1_VBUS_EN |      \
	 SM5803_GPADCC1_IBAT_CHG_EN | SM5803_GPADCC1_IBAT_DIS_EN | \
	 SM5803_GPADCC1_VBATSNSP_EN)

#define SM5803_REG_GPADC_CONFIG2 0x02

#define SM5803_REG_PSYS1 0x04
#define SM5803_PSYS1_DAC_EN BIT(0)

/* Note: Threshold registers all assume lower 2 bits are 0 */
#define SM5803_REG_VBUS_LOW_TH 0x1A
#define SM5803_REG_VBATSNSP_MAX_TH 0x26
#define SM5803_REG_VBUS_HIGH_TH 0x2A
#define SM5803_REG_VCHG_PWR_LOW_TH 0x1B
#define SM5803_REG_VCHG_PWR_HIGH_TH 0x2B
#define SM5803_REG_TINT_LOW_TH 0x1D
#define SM5803_REG_TINT_HIGH_TH 0x2D

/*
 * Vbus levels increment in 23.4 mV, set thresholds to below 3.5V and above 4.0V
 * to mirror what TCPCI uses for Vbus present indication
 */
#define SM5803_VBUS_LOW_LEVEL 0x25
#define SM5803_VBUS_HIGH_LEVEL 0x2C

/*
 * TINT thresholds.  TINT steps are in 0.43 K with the upper threshold set to
 * 360 K and lower threshold to de-assert PROCHOT at 330 K.
 */
#define SM5803_TINT_LOW_LEVEL 0xBF
#define SM5803_TINT_HIGH_LEVEL 0xD1

#define SM5803_TINT_MAX_LEVEL 0xFF
#define SM5803_TINT_MIN_LEVEL 0x00

/*
 * Set minimum thresholds for VBUS_PWR_LOW_TH interrupt generation
 * 2S battery 9.4v
 * 3S battery 14.1V VBUS_PWR MIN TH
 */
#define SM5803_VBAT_PWR_MINTH_3S_LEVEL 0x9B
#define SM5803_VBAT_PWR_MINTH_2S_LEVEL 0x9B

/*
 * Set thresholds for VBATSNSP_MAX_TH GPADC interrupt generation
 * 2S battery 9v
 * 3S battery 13.3V
 */
#define SM5803_VBAT_SNSP_MAXTH_3S_LEVEL 0xD8
#define SM5803_VBAT_SNSP_MAXTH_2S_LEVEL 0xDC

/* IBAT levels - The IBAT levels increment in 7.32mA */
#define SM5803_REG_IBAT_CHG_MEAS_MSB 0x44
#define SM5803_REG_IBAT_CHG_MEAS_LSB 0x45
#define SM5803_REG_IBAT_CHG_AVG_MEAS_MSB 0xC4
#define SM5803_REG_IBAT_CHG_AVG_MEAS_LSB 0xC5
#define SM5803_IBAT_CHG_MEAS_LSB GENMASK(1, 0)

/* IBUS levels - The IBUS levels increment in 7.32mA */
#define SM5803_REG_IBUS_CHG_MEAS_MSB 0x46
#define SM5803_REG_IBUS_CHG_MEAS_LSB 0x47
#define SM5803_IBUS_CHG_MEAS_LSB GENMASK(1, 0)

#define SM5803_REG_VBUS_MEAS_MSB 0x48
#define SM5803_REG_VBUS_MEAS_LSB 0x49
#define SM5803_VBUS_MEAS_LSB GENMASK(1, 0)
#define SM5803_VBUS_MEAS_BAT_DET BIT(2)
#define SM5803_VBUS_MEAS_VBUS_SHORT BIT(4)
#define SM5803_VBUS_MEAS_OV_TEMP BIT(5)
#define SM5803_VBUS_MEAS_CHG_DET BIT(6)

/* VCHGPWR levels - The VCHGPWR levels increment in 23.4mV steps. */
#define SM5803_REG_VCHG_PWR_MSB 0x4A

#define SM5803_REG_TINT_MEAS_MSB 0x4E

/* VSYS levels - The VSYS levels increment in  23.4mV steps. */
#define SM5803_REG_VSYS_MEAS_MSB 0x4C
#define SM5803_REG_VSYS_MEAS_LSB 0x4D
#define SM5803_REG_VSYS_AVG_MEAS_MSB 0xCC
#define SM5803_REG_VSYS_AVG_MEAS_LSB 0xCD
#define SM5803_VSYS_MEAS_LSB GENMASK(1, 0)

/* Charger registers (address 0x32) */

#define SM5803_REG_CC_CONFIG1 0x01
#define SM5803_CC_CONFIG1_SD_PWRUP BIT(3)

#define SM5803_REG_FLOW1 0x1C
#define SM5803_FLOW1_MODE GENMASK(1, 0)
#define SM5803_FLOW1_DIRECTCHG_SRC_EN BIT(2)
#define SM5803_FLOW1_LINEAR_CHARGE_EN BIT(3)
#define SM5803_FLOW1_USB_SUSP BIT(7)

enum sm5803_charger_modes {
	CHARGER_MODE_DISABLED,
	CHARGER_MODE_SINK,
	CHARGER_MODE_RESERVED,
	CHARGER_MODE_SOURCE,
};

#define SM5803_REG_FLOW2 0x1D
#define SM5803_FLOW2_AUTO_TRKL_EN BIT(0)
#define SM5803_FLOW2_AUTO_PRECHG_EN BIT(1)
#define SM5803_FLOW2_AUTO_FASTCHG_EN BIT(2)
#define SM5803_FLOW2_AUTO_ENABLED GENMASK(2, 0)
#define SM5803_FLOW2_FW_TRKL_CMD BIT(3)
#define SM5803_FLOW2_FW_PRECHG_CMD BIT(4)
#define SM5803_FLOW2_FW_FASTCHG_CMD BIT(5)
#define SM5803_FLOW2_HOST_MODE_EN BIT(6)
#define SM5803_FLOW2_AUTO_CHGEN_SET BIT(7)

#define SM5803_REG_FLOW3 0x1E
#define SM5803_FLOW3_SWITCH_BCK_BST BIT(0)
#define SM5803_FLOW3_FW_SWITCH_RESUME BIT(1)
#define SM5803_FLOW3_FW_SWITCH_PAUSE BIT(2)
#define SM5803_FLOW3_SOFT_DISABLE_EN BIT(3)

#define SM5803_REG_SWITCHER_CONF 0x1F
#define SM5803_SW_BCK_BST_CONF_AUTO BIT(0)

#define SM5803_REG_ANA_EN1 0x21
#define SM5803_ANA_EN1_CLS_DISABLE BIT(7)

/*
 * Input current limit is CHG_ILIM_RAW *100 mA
 */
#define SM5803_REG_CHG_ILIM 0x24
#define SM5803_CHG_ILIM_RAW GENMASK(4, 0)
#define SM5803_CURRENT_STEP 100
#define SM5803_REG_TO_CURRENT(r) ((r) * SM5803_CURRENT_STEP)
#define SM5803_CURRENT_TO_REG(c) ((c) / SM5803_CURRENT_STEP)

/*
 * DPM Voltage loop regulation contains the 8 bits with MSB register
 * and the lower 3 bits with LSB register.
 * The regulation value is 2.72 V + DPM_VL_SET * 10mV
 */
#define SM5803_REG_DPM_VL_SET_MSB 0x26
#define SM5803_REG_DPM_VL_SET_LSB 0x27

/*
 * Output voltage uses the same equation as Vsys
 * Lower saturation value is 3 V, upper 20.5 V
 */
#define SM5803_REG_VPWR_MSB 0x30
#define SM5803_REG_DISCH_CONF2 0x31
#define SM5803_DISCH_CONF5_VPWR_LSB GENMASK(2, 0)

/*
 * Output current limit is CLS_LIMIT * 50 mA and saturates to 3.2 A
 */
#define SM5803_REG_DISCH_CONF5 0x34
#define SM5803_DISCH_CONF5_CLS_LIMIT GENMASK(6, 0)
#define SM5803_CLS_CURRENT_STEP 50

#define SM5803_REG_DISCH_CONF6 0x35
#define SM5803_DISCH_CONF6_RAMPS_DIS BIT(0)
#define SM5803_DISCH_CONF6_SMOOTH_DIS BIT(1)

/*
 * Vsys is 11 bits, with the lower 3 bits in the LSB register.
 * The pre-regulation value is 2.72 V + Vsys_prereg * 10 mV
 * Lower saturation value is 3V, upper is 20V
 */
#define SM5803_REG_VSYS_PREREG_MSB 0x36
#define SM5803_REG_VSYS_PREREG_LSB 0x37
#define SM5803_VOLTAGE_STEP 10
#define SM5803_VOLTAGE_SHIFT 2720
#define SM5803_REG_TO_VOLTAGE(r) \
	(SM5803_VOLTAGE_SHIFT + (r) * SM5803_VOLTAGE_STEP)
#define SM5803_VOLTAGE_TO_REG(v) \
	(((v)-SM5803_VOLTAGE_SHIFT) / SM5803_VOLTAGE_STEP)

/*
 * Precharge Termination threshold.
 */
#define SM5803_REG_PRE_FAST_CONF_REG1 0x39
#define SM5803_VBAT_PRE_TERM_MIN_DV 23
/* 3.8V+ gets rounded to 4V */
#define SM5803_VBAT_PRE_TERM_MAX_DV 38
#define SM5803_VBAT_PRE_TERM GENMASK(7, 4)
#define SM5803_VBAT_PRE_TERM_SHIFT 4

/*
 * Vbat for fast charge uses the same equation as Vsys
 * Lower saturation value is 3V, upper is dependent on number of cells
 */
#define SM5803_REG_VBAT_FAST_MSB 0x3A
#define SM5803_REG_VBAT_FAST_LSB 0x3B

/*
 * Fast charge current limit is ICHG_FAST * 100 mA
 * Value read back may be adjusted if tempearture limits are exceeded
 */
#define SM5803_REG_FAST_CONF4 0x3C
#define SM5803_CONF4_ICHG_FAST GENMASK(5, 0)

/* Fast charge Termination */
#define SM5803_REG_FAST_CONF5 0x3D
#define SM5803_CONF5_IBAT_EOC_TH GENMASK(3, 0)

/* IR drop compensation */
#define SM5803_REG_IR_COMP1 0x3F
#define SM5803_IR_COMP_RES_SET_MSB GENMASK(7, 6)
#define SM5803_IR_COMP_RES_SET_MSB_SHIFT 6
#define SM5803_IR_COMP_EN BIT(5)

/* LSB is in 1.67mOhm steps. */
#define SM5803_REG_IR_COMP2 0x40

/* Precharge current limit is also intervals of 100 mA */
#define SM5803_REG_PRECHG 0x41
#define SM5803_PRECHG_ICHG_PRE_SET GENMASK(5, 0)

#define SM5803_REG_LOG1 0x42
#define SM5803_BATFET_ON BIT(2)

#define SM5803_REG_LOG2 0x43
#define SM5803_ISOLOOP_ON BIT(1)

#define SM5803_REG_STATUS_CHG_REG 0x48
#define SM5803_STATUS_CHG_BATT_REMOVAL BIT(0)
#define SM5803_STATUS_CHG_CHG_REMOVAL BIT(1)
#define SM5803_STATUS_CHG_BATTEMP_NOK BIT(2)
#define SM5803_STATUS_CHG_CHGWDG_EXP BIT(3)
#define SM5803_STATUS_CHG_VBUS_OC BIT(4)
#define SM5803_STATUS_CHG_OV_VBAT BIT(5)
#define SM5803_STATUS_CHG_TIMEOUT BIT(6)
#define SM5803_STATUS_CHG_OV_ITEMP BIT(7)

#define SM5803_REG_STATUS_DISCHG 0x49
#define SM5803_STATUS_DISCHG_BATT_REM BIT(0)
#define SM5803_STATUS_DISCHG_UV_VBAT BIT(1)
#define SM5803_STATUS_DISCHG_VBUS_OC BIT(2)
#define SM5803_STATUS_DISCHG_VBUS_PWR GENMASK(4, 3)
#define SM5803_STATUS_DISCHG_ISO_CURR BIT(5)
#define SM5803_STATUS_DISCHG_VBUS_SHORT BIT(6)
#define SM5803_STATUS_DISCHG_OV_ITEMP BIT(7)

#define SM5803_REG_CHG_MON_REG 0x5C
#define SM5803_DPM_LOOP_EN BIT(0)

#define SM5803_REG_PHOT1 0x72
#define SM5803_PHOT1_IBAT_PHOT_COMP_EN BIT(0)
#define SM5803_PHOT1_IBUS_PHOT_COMP_EN BIT(1)
#define SM5803_PHOT1_VSYS_MON_EN BIT(2)
#define SM5803_PHOT1_VBUS_MON_EN BIT(3)
#define SM5803_PHOT1_COMPARATOR_EN GENMASK(3, 0)
#define SM5803_PHOT1_DURATION GENMASK(6, 4)
#define SM5803_PHOT1_DURATION_SHIFT 4
#define SM5803_PHOT1_IRQ_MODE BIT(7)

enum sm5803_phot1_duration {
	PHOT1_DURATION_100us = 0,
	PHOT1_DURATION_500us = 1,
	PHOT1_DURATION_1ms = 2,
	PHOT1_DURATION_2ms = 4,
	PHOT1_DURATION_5ms = 6,
	PHOT1_DURATION_10ms = 7
};

#define SM5803_REG_PHOT2 0x73
#define SM5803_PHOT2_VBUS_SEL GENMASK(4, 0)

enum sm5803_phot2_vbus_sel {
	PHOT2_VBUS_SEL_3V = 0,
	PHOT2_VBUS_SEL_3P25V,
	PHOT2_VBUS_SEL_3P5V,
	PHOT2_VBUS_SEL_3P75V,
	PHOT2_VBUS_SEL_4V,
	PHOT2_VBUS_SEL_4P25V,
	PHOT2_VBUS_SEL_4P5V,
	PHOT2_VBUS_SEL_4P75V,
	PHOT2_VBUS_SEL_5V,
	PHOT2_VBUS_SEL_5P5V,
	PHOT2_VBUS_SEL_6V,
	PHOT2_VBUS_SEL_6P5V,
	PHOT2_VBUS_SEL_7V,
	PHOT2_VBUS_SEL_7P5V,
	PHOT2_VBUS_SEL_8V,
	PHOT2_VBUS_SEL_8P5V,
	PHOT2_VBUS_SEL_9V,
	PHOT2_VBUS_SEL_9P5V,
	PHOT2_VBUS_SEL_10V,
	PHOT2_VBUS_SEL_10P5V,
	PHOT2_VBUS_SEL_11V,
	PHOT2_VBUS_SEL_11P5V,
	PHOT2_VBUS_SEL_12V,
	PHOT2_VBUS_SEL_12P5V,
	PHOT2_VBUS_SEL_13V,
	PHOT2_VBUS_SEL_13P5V,
	PHOT2_VBUS_SEL_14V,
	PHOT2_VBUS_SEL_15V,
	PHOT2_VBUS_SEL_16V,
	PHOT2_VBUS_SEL_17V,
	PHOT2_VBUS_SEL_18V,
	PHOT2_VBUS_SEL_19V
};

#define SM5803_REG_PHOT3 0x74
#define SM5803_PHOT3_VSYS_SEL GENMASK(4, 0)

enum sm5803_phot3_vbus_sel {
	PHOT3_VSYS_SEL_3V = 0,
	PHOT3_VSYS_SEL_3P25V,
	PHOT3_VSYS_SEL_3P5V,
	PHOT3_VSYS_SEL_3P75V,
	PHOT3_VSYS_SEL_4V,
	PHOT3_VSYS_SEL_4P25V,
	PHOT3_VSYS_SEL_4P5V,
	PHOT3_VSYS_SEL_4P75V,
	PHOT3_VSYS_SEL_5V,
	PHOT3_VSYS_SEL_5P5V,
	PHOT3_VSYS_SEL_6V,
	PHOT3_VSYS_SEL_6P5V,
	PHOT3_VSYS_SEL_7V,
	PHOT3_VSYS_SEL_7P5V,
	PHOT3_VSYS_SEL_8V,
	PHOT3_VSYS_SEL_8P5V,
	PHOT3_VSYS_SEL_9V,
	PHOT3_VSYS_SEL_9P5V,
	PHOT3_VSYS_SEL_10V,
	PHOT3_VSYS_SEL_10P5V,
	PHOT3_VSYS_SEL_11V,
	PHOT3_VSYS_SEL_11P5V,
	PHOT3_VSYS_SEL_12V,
	PHOT3_VSYS_SEL_12P5V,
	PHOT3_VSYS_SEL_13V,
	PHOT3_VSYS_SEL_13P5V,
	PHOT3_VSYS_SEL_14V,
	PHOT3_VSYS_SEL_15V,
	PHOT3_VSYS_SEL_16V,
	PHOT3_VSYS_SEL_17V,
	PHOT3_VSYS_SEL_18V,
	PHOT3_VSYS_SEL_19V
};

#define SM5803_REG_PHOT4 0x75
#define SM5803_PHOT4_IBAT_SEL GENMASK(5, 0)

#define CHARGER_NAME "sm5803"

#define CHARGE_V_MAX 20000
#define CHARGE_V_MIN SM5803_VOLTAGE_SHIFT
#define CHARGE_V_STEP SM5803_VOLTAGE_STEP

#define CHARGE_I_MAX 6300
#define CHARGE_I_MIN 0
#define CHARGE_I_STEP SM5803_CURRENT_STEP

#define INPUT_I_MAX 3100
#define INPUT_I_MIN 0
#define INPUT_I_STEP SM5803_CURRENT_STEP

/*
 * Maximum IBAT select threshold is [5:0] = 111111
 * and the value is IBAT_PHOT_SEL * 600 mA
 */
#define IBAT_SEL_MAX 37800

#define SM5803_IBAT_PROCHOT_MA_TO_REG(ma) MIN(ma / 600, SM5803_PHOT4_IBAT_SEL)

/* Expose cached Vbus presence */
int sm5803_is_vbus_present(int chgnum);

/* Expose functions to control charger's GPIO and CHG_DET configuration */
enum ec_error_list sm5803_configure_gpio0(int chgnum,
					  enum sm5803_gpio0_modes mode, int od);
enum ec_error_list sm5803_set_gpio0_level(int chgnum, int level);
enum ec_error_list sm5803_configure_chg_det_od(int chgnum, int enable);
enum ec_error_list sm5803_get_chg_det(int chgnum, int *chg_det);

/* Expose Vbus discharge function */
enum ec_error_list sm5803_set_vbus_disch(int chgnum, int enable);
enum ec_error_list sm5803_vbus_sink_enable(int chgnum, int enable);

void sm5803_hibernate(int chgnum);
void sm5803_interrupt(int chgnum);

/**
 * Return whether ACOK is high or low.
 *
 * @param chgnum index into chg_chips table.
 * @param acok will be set to true if ACOK is asserted, otherwise false.
 * @return EC_SUCCESS, error otherwise.
 */
enum ec_error_list sm5803_is_acok(int chgnum, bool *acok);

/**
 * Test whether the current voltage on VBUS corresponds to the given range.
 *
 * Users should prefer this function to manually evaluating the result of
 * charger_get_vbus_voltage because that function may behave incorrectly when
 * the charger is in low power mode. This function will return correct results
 * regardless of the charger state.
 *
 * @param chgnum charger index to test
 * @param level VBUS range
 * @return true if the current VBUS voltage is in the given range, false if it
 *         is not or if there is a problem communicating with the charger.
 */
bool sm5803_check_vbus_level(int chgnum, enum vbus_level level);

/* Expose low power mode functions */
void sm5803_disable_low_power_mode(int chgnum);
void sm5803_enable_low_power_mode(int chgnum);

extern const struct charger_drv sm5803_drv;

/* Expose interrupt handler for processing in PD_INT task when needed */
void sm5803_handle_interrupt(int chgnum);

#ifdef TEST_BUILD
void test_sm5803_set_fast_charge_disabled(bool value);
bool test_sm5803_get_fast_charge_disabled(void);
extern int charge_idle_enabled;
#endif

enum ec_error_list
sm5803_set_phot_duration(int chgnum, enum sm5803_phot1_duration duration);
enum ec_error_list
sm5803_set_vbus_monitor_sel(int chgnum, enum sm5803_phot2_vbus_sel vbus_sel);
enum ec_error_list
sm5803_set_vsys_monitor_sel(int chgnum, enum sm5803_phot3_vbus_sel vsys_sel);
enum ec_error_list sm5803_set_ibat_phot_sel(int chgnum, int ibat_sel);
#endif
