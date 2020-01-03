/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Mitus SM5803 Buck-Boost Charger
 */

#ifndef __CROS_EC_SM5803_H
#define __CROS_EC_SM5803_H

/* Note: configure charger struct with CHARGER_FLAGS */
#define SM5803_ADDR_MAIN_FLAGS		0x30
#define SM5803_ADDR_MEAS_FLAGS		0x31
#define SM5803_ADDR_CHARGER_FLAGS	0x32

/* Main registers (address 0x30) */

#define SM5803_REG_STATUS1		0x01
#define SM5803_STATUS1_VSYS_OK		BIT(0)
#define SM5803_STATUS1_VPWR_OK		BIT(1)
#define SM5803_STATUS1_VBUS_UVL		BIT(3)
#define SM5803_STATUS1_VBUS_SHORT	BIT(4)
#define SM5803_STATUS1_VBUS_OVH		BIT(5)
#define SM5803_STATUS1_CHG_DET		BIT(6)
#define SM5803_STATUS1_BAT_DET		BIT(7)

#define SM5803_REG_STATUS2		0x02
#define SM5803_STATUS2_BAT_DET_FG	BIT(1)
#define SM5803_STATUS2_VBAT_SHORT	BIT(0)

#define SM5803_REG_INT1_REQ			0x05
#define SM5803_REG_INT1_EN			0x0A
#define SM5803_INT1_VBUS_PWR_HWSAFE_LIMIT	BIT(0)
#define SM5803_INT1_CHG				BIT(2)
#define SM5803_INT1_BAT				BIT(3)
#define SM5803_INT1_CLS_OC			BIT(4)
#define SM5803_INT1_SLV_DET			BIT(5)
#define SM5803_INT1_SWL_DISCH			BIT(6)
#define SM5803_INT1_PREREG			BIT(7)

#define SM5803_REG_INT2_REQ		0x06
#define SM5803_REG_INT2_EN		0x0B
#define SM5803_INT2_VBATSNSP		BIT(0)
#define SM5803_INT2_IBAT_DISCHG		BIT(1)
#define SM5803_INT2_IBAT_CHG		BIT(2)
#define SM5803_INT2_IBUS		BIT(3)
#define SM5803_INT2_VBUS		BIT(4)
#define SM5803_INT2_VCHGPWR		BIT(5)
#define SM5803_INT2_VSYS		BIT(6)
#define SM5803_INT2_TINT		BIT(7)

#define SM5803_REG_INT3_REQ		0x07
#define SM5803_REG_INT3_EN		0x0C
#define SM5803_INT3_GPADC0		BIT(0)
#define SM5803_INT3_VBATC1		BIT(1)
#define SM5803_INT3_VBATC2		BIT(2)
#define SM5803_INT3_SPARE		BIT(3)
#define SM5803_INT3_VBUS_PWR_LIMIT	BIT(4)
#define SM5803_INT3_IBAT		BIT(5)

#define SM5803_REG_INT4_REQ		0x08
#define SM5803_REG_INT4_EN		0x0D
#define SM5803_INT4_CHG_FAIL		BIT(0)
#define SM5803_INT4_CHG_DONE		BIT(1)
#define SM5803_INT4_CHG_START		BIT(2)
#define SM5803_INT4_SLP_EXIT		BIT(3)
#define SM5803_INT4_OTG_FAIL		BIT(4)
#define SM5803_INT4_CHG_ILIM		BIT(5)
#define SM5803_INT4_IBAT_CC		BIT(6)
#define SM5803_INT4_CC			BIT(7)

#define SM5803_REG_MISC_CONFIG		0x15
#define SM5803_MISC_INV_INT		BIT(0)
#define SM5803_INT_CLEAR_MODE		BIT(1)
#define SM5803_INT_MASK_MODE		BIT(2)

#define SM5803_REG_REFERENCE		0x20
#define SM5803_REFERENCE_LDO3P3_PGOOD	BIT(4)
#define SM5803_REFERENCE_LDO5_PGOOD	BIT(5)

#define SM5803_REG_GPIO0_CTRL		0x30
#define SM5803_GPIO0_VAL		BIT(0)
#define SM5803_GPIO0_MODE_MASK		GENMASK(2, 1)
#define SM5803_GPIO0_OPEN_DRAIN_EN	BIT(6)
#define SM5803_CHG_DET_OPEN_DRAIN_EN	BIT(7)

enum sm5803_gpio0_modes {
	GPIO0_MODE_PROCHOT,
	GPIO0_MODE_OUTPUT,
	GPIO0_MODE_INPUT
};

#define SM5803_REG_PORTS_CTRL		0x40
#define SM5803_PORTS_VBUS_DISCH		BIT(0)
#define SM5803_PORTS_VBUS_PULLDOWN	BIT(1)
#define SM5803_PORTS_VBUS_SNS_DISCH	BIT(2)
#define SM5803_PORTS_VBUS_SNS_PULLDOWN	BIT(3)

/* ADC Registers (address 0x31) */

#define SM5803_REG_GPADC_CONFIG1	0x01
#define SM5803_GPADCC1_VBATSNSP_EN	BIT(0)
#define SM5803_GPADCC1_IBAT_DIS_EN	BIT(1)
#define SM5803_GPADCC1_IBAT_CHG_EN	BIT(2)
#define SM5803_GPADCC1_IBUS_EN		BIT(3)
#define SM5803_GPADCC1_VBUS_EN		BIT(4)
#define SM5803_GPADCC1_VCHGPWR_EN	BIT(5)
#define SM5803_GPADCC1_VSYS_EN		BIT(6)
#define SM5803_GPADCC1_TINT_EN		BIT(7)

/* Note: Threshold registers all assume lower 2 bits are 0 */
#define SM5803_REG_VBUS_LOW_TH		0x1B
#define SM5803_REG_VBUS_HIGH_TH		0x2B
#define SM5803_REG_TINT_HIGH_TH		0x2D

/*
 * Vbus levels increment in 23.4 mV, set thresholds to below 3.5V and above 4.0V
 * to mirror what TCPCI uses for Vbus present indication
 */
#define SM5803_VBUS_LOW_LEVEL		0x25
#define SM5803_VBUS_HIGH_LEVEL		0x2C

#define SM5803_REG_VBUS_MEAS_MSB	0x48
#define SM5803_REG_VBUS_MEAS_LSB	0x49
#define SM5803_VBUS_MEAS_LSB		GENMASK(1, 0)
#define SM5803_VBUS_MEAS_BAT_DET	BIT(2)
#define SM5803_VBUS_MEAS_VBUS_SHORT	BIT(4)
#define SM5803_VBUS_MEAS_OV_TEMP	BIT(5)
#define SM5803_VBUS_MEAS_CHG_DET	BIT(6)

/* Charger registers (address 0x32) */

#define SM5803_REG_FLOW1		0x1C
#define SM5803_FLOW1_CHG_EN		BIT(0)
#define SM5803_FLOW1_VBUSIN_DISCHG_EN	BIT(1)
#define SM5803_FLOW1_DIRECTCHG_SRC_EN	BIT(2)
#define SM5803_FLOW1_USB_SUSP		BIT(7)

#define SM5803_REG_FLOW2		0x1D
#define SM5803_FLOW2_AUTO_TRKL_EN	BIT(0)
#define SM5803_FLOW2_AUTO_PRECHG_EN	BIT(1)
#define SM5803_FLOW2_AUTO_FASTCHG_EN	BIT(2)
#define SM5803_FLOW2_AUTO_ENABLED	GENMASK(2, 0)
#define SM5803_FLOW2_FW_TRKL_CMD	BIT(3)
#define SM5803_FLOW2_FW_PRECHG_CMD	BIT(4)
#define SM5803_FLOW2_FW_FASTCHG_CMD	BIT(5)
#define SM5803_FLOW2_HOST_MODE_EN	BIT(6)
#define SM5803_FLOW2_AUTO_CHGEN_SET	BIT(7)

#define SM5803_REG_FLOW3		0x1E
#define SM5803_FLOW3_SWITCH_BCK_BST	BIT(0)
#define SM5803_FLOW3_FW_SWITCH_RESUME	BIT(1)
#define SM5803_FLOW3_FW_SWITCH_PAUSE	BIT(2)
#define SM5803_FLOW3_SOFT_DISABLE_EN	BIT(3)

/*
 * Input current limit is CHG_ILIM_RAW *100 mA
 */
#define SM5803_REG_CHG_ILIM		0x24
#define SM5803_CHG_ILIM_RAW		GENMASK(4, 0)
#define SM5803_CURRENT_STEP		100
#define SM5803_REG_TO_CURRENT(r)	(r * SM5803_CURRENT_STEP)
#define SM5803_CURRENT_TO_REG(c)	(c / SM5803_CURRENT_STEP)

/*
 * Vsys is 11 bits, with the lower 3 bits in the LSB register.
 * The pre-regulation value is 2.72 V + Vsys_prereg * 10 mV
 * Lower saturation value is 3V, upper is 20V
 */
#define SM5803_REG_VSYS_PREREG_MSB	0x36
#define SM5803_REG_VSYS_PREREG_LSB	0x37
#define SM5803_VOLTAGE_STEP		10
#define SM5803_VOLTAGE_SHIFT		2720
#define SM5803_REG_TO_VOLTAGE(r)	(SM5803_VOLTAGE_SHIFT + \
					 r * SM5803_VOLTAGE_STEP)
#define SM5803_VOLTAGE_TO_REG(v)	((v - SM5803_VOLTAGE_SHIFT) \
					 / SM5803_VOLTAGE_STEP)

/*
 * Vbat for fast charge uses the same equation as Vsys
 * Lower saturation value is 3V, upper is dependent on number of cells
 */
#define SM5803_REG_VBAT_FAST_MSB	0x3A
#define SM5803_REG_VBAT_FAST_LSB	0x3B

/*
 * Fast charge current limit is ICHG_FAST * 100 mA
 * Value read back may be adjusted if tempearture limits are exceeded
 */
#define SM5803_REG_FAST_CONF4		0x3C
#define SM5803_CONF4_ICHG_FAST		GENMASK(5, 0)

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

/* Expose functions to control charger's GPIO */
enum ec_error_list sm5803_configure_gpio0(int chgnum,
					  enum sm5803_gpio0_modes mode);
enum ec_error_list sm5803_set_gpio0_level(int chgnum, int level);

void sm5803_handle_interrupt(int chgnum);

extern const struct charger_drv sm5803_drv;

#endif
