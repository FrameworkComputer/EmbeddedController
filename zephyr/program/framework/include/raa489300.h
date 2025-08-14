/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RAA489300_H
#define __CROS_EC_RAA489300_H

/* I2C address */
#define RAA489300_ADDR_FLAGS 0x4a
/* Registers */
#define RAA489300_REG_OUTPUT_CURRENT_LIMIT	0x14
#define RAA489300_REG_OUTPUT_VOLTAGE		0x15
#define RAA489300_REG_CONTROL0				0x39
#define RAA489300_REG_INFORMATION1			0x3A
#define RAA489300_REG_CONTROL1				0x3C
#define RAA489300_REG_CONTROL2				0x3D
#define RAA489300_REG_INPUT_CURRENT_LIMIT	0x3F
#define RAA489300_REG_VINOK_REFERENCE		0x40
#define RAA489300_REG_CONTROL6				0x43
#define RAA489300_REG_REVERSE_PTM_VOLTAGE	0x49
#define RAA489300_REG_MIN_INPUT_VOLTAGE		0x4B
#define RAA489300_REG_CONTROL3				0x4C
#define RAA489300_REG_INFORMATION2			0x4D
#define RAA489300_REG_CONTROL4				0x4E
#define RAA489300_REG_CONTROL5				0x4F
#define RAA489300_REG_INFORMATION3			0x90
#define RAA489300_REG_INFORMATION4			0x91
#define RAA489300_REG_MANUFACTURER_ID		0xFE
#define RAA489300_REG_DEVICE_ID				0xFF

#define RAA489300_REG_OUTPUT_CURRENT_LIMIT_5P5A	0x1580
#define RAA489300_REG_OUTPUT_CURRENT_LIMIT_7A	0x1B58
#define RAA489300_REG_OUTPUT_VOLTAGE_18V		0x2EE0
#define RAA489300_REG_OUTPUT_VOLTAGE_20V		0x3410
#define RAA489300_REG_OUTPUT_VOLTAGE_24V		0x3E80

/*
 * ========= CONTROL0 (0x39h) =========
 */
#define RAA489300_C0_PRECHARGE_100MS		(2 << 13)
#define RAA489300_C0_ALERT_720US			(2 << 11)
#define RAA489300_C0_ENABLE_PTM_MODE		BIT(1)
#define RAA489300_C0_ENABLE_SWITCHING		BIT(0)

#define RAA489300_C0_VALUE \
	(RAA489300_C0_PRECHARGE_100MS | \
	 RAA489300_C0_ALERT_720US     | \
	 RAA489300_C0_ENABLE_SWITCHING)

/*
 * ========= CONTROL1 (0x3Ch) =========
 */
#define RAA489300_C1_HIGH_SIDE_PHASE_8MV	(8 << 12)
/* [7] Current Feedback Gain: 0 = 1x, 1 = 0.5x */
#define RAA489300_C1_CURR_GAIN			BIT(7)
/* [5] IMON Select: 0 = Output current, 1 = Input current */
#define RAA489300_C1_IMON_SELECT		BIT(5)
#define RAA489300_C1_VOUT_OVP_24V		(0 << 2)
#define RAA489300_C1_VOUT_OVP_33V		(1 << 2)

#define RAA489300_C1_VALUE \
	(RAA489300_C1_CURR_GAIN | RAA489300_C1_IMON_SELECT)

/*
 * ========= CONTROL2 (0x3Dh) =========
 */
/* [13] CSIN/CSOP discharge current: 0=20mA, 1=30mA */
#define RAA489300_C2_DISCHARGE_CURR_30MA		BIT(13)
#define RAA489300_C2_LOW_POWER_PTM_MODE			BIT(12)
#define RAA489300_C2_ENABLE_AUTO_DISCHARGE		BIT(11)
#define RAA489300_C2_PGOOD_WINDOW_5				(0 << 8)
#define RAA489300_C2_PGOOD_WINDOW_10			(1 << 8)
#define RAA489300_C2_PGOOD_WINDOW_15			(2 << 8)
#define RAA489300_C2_PGOOD_WINDOW_20			(3 << 8)
#define RAA489300_C2_ENABLE_CFLY_PRECHARGE		BIT(7)
/* [4] Fault Retry Timer: 0=1.3s, 1=150ms */
#define RAA489300_C2_FAULT_RETRY_150MS			BIT(4)

#define RAA489300_C2_VALUE_SPR \
	(RAA489300_C2_DISCHARGE_CURR_30MA | \
	 RAA489300_C2_ENABLE_AUTO_DISCHARGE | \
	 RAA489300_C2_PGOOD_WINDOW_20 | \
	 RAA489300_C2_FAULT_RETRY_150MS)

/*
 * ========= CONTROL3 (0x4Ch) =========
 */

#define RAA489300_C3_USB_ARC_PREVENT		BIT(12)
#define RAA489300_C3_ENABLE_ADC		BIT(0)

#define RAA489300_C3_VALUE \
	(RAA489300_C3_USB_ARC_PREVENT | RAA489300_C3_ENABLE_ADC)

/*
 * ========= CONTROL4 (0x4Eh) =========
 */
#define RAA489300_C4_LS_BLANK_40NS		(0 << 8)
#define RAA489300_C4_LS_BLANK_80NS		(1 << 8)
#define RAA489300_C4_LS_BLANK_120NS		(2 << 8)
#define RAA489300_C4_LS_BLANK_160NS		(3 << 8)
#define RAA489300_C4_LS_BANDWIDTH_15HZ		(0 << 6)
#define RAA489300_C4_LS_BANDWIDTH_7HZ		(1 << 6)
#define RAA489300_C4_LS_BANDWIDTH_4HZ		(2 << 6)
#define RAA489300_C4_LS_BANDWIDTH_3HZ		(3 << 6)

#define RAA489300_C4_VALUE \
	(RAA489300_C4_LS_BLANK_80NS | RAA489300_C4_LS_BANDWIDTH_7HZ)

/*
 * ========= CONTROL5 (0x4Fh) =========
 */
#define RAA489300_C5_DIS_CFLY_UV_OV			BIT(11)
#define RAA489300_C5_DISABLE_GP_COMPARATOR	BIT(0)

#define RAA489300_C5_VALUE \
	(RAA489300_C5_DIS_CFLY_UV_OV | RAA489300_C5_DISABLE_GP_COMPARATOR)


/* Operating Mode [bits 13:12]*/
#define OPER_MASK						GENMASK(13, 12)
#define OPER_MODE_OFF					0
#define OPER_MODE_REVERSE_PTM			(1 << 12)
#define OPER_MODE_FORWARD_PTM			(2 << 12)
#define OPER_MODE_FORWARD_BUCK			(3 << 12)
/* Power State Machine states [bits 11:8] */
#define PSM_MASK						GENMASK(11, 8)
#define PSM_RESET_STATE					0
#define PSM_SLEEP_STATE					(1 << 8)
#define PSM_PRECHARGE_STATE				(2 << 8)
#define PSM_READY_STATE					(3 << 8)
#define PSM_REVERSE_PTM_STATE			(4 << 8)
#define PSM_FORWARD_SWITCHING_STATE		(5 << 8)
#define PSM_FORWARD_PTM_STATE			(6 << 8)
#define PSM_FAULT_LATCHOFF_STATE		(7 << 8)
#define PSM_RETRY_FAULT_STATE			(8 << 8)
#define PSM_AUTO_DISCHARGE_STATE		(9 << 8)

#define RAA489300_STATE_MASK		(OPER_MASK | PSM_MASK)

/*
 * VINOK_REFERENCE Register (0x40)
 * MinInputVoltage Register (0x4B)
 * Maximum [15:8] = 10110100 = 180 decimal
 */
#define RAA489300_MV_TO_VIN(mv) ((MIN((mv) / 257, 180) << 8))

/*
 * OutputVoltage Register (0x15)
 */
#define PPS_VOLTAGE_STEP_MV 12
#define PPS_VOLTAGE_MAX 24564
#define AVS_VOLTAGE_STEP_MV 24
#define AVS_VOLTAGE_MAX 54912

enum level_buck_mode {
	LEVEL_BUCK_SPR,
	LEVEL_BUCK_EPR,
	LEVEL_BUCK_ENTER_EPR,
	LEVEL_BUCK_EXIT_EPR,
	LEVEL_BUCK_DC,
};

int write_level_buck_registers(enum level_buck_mode mode);

/**
 * Check the expected state machine for a given mode.
 *
 * @param mode	Target mode (e.g., SPR / EPR / DC).
 * @param data	Output pointer; returns the INFORMATION1 register value
 *
 * @return EC_SUCCESS if the state matches the expectation
 */
int level_buck_check_expected_state(enum level_buck_mode mode, int *data);

void board_level_buck_update(void);

void level_buck_set_acok_reference(int mv);

int level_buck_set_input_current_limit(int ma);

int level_buck_set_output_current_limit(int ma);

int level_buck_set_output_voltage(int mv);

/**
 * Enabled low power PTM mode to reduces the power consumed.
 *
 * @param enabled 1: enabled, 0: disabled
 */
void raa489300_enter_low_power_ptm_mode(bool enabled);

#endif	/* __CROS_EC_RAA489300_H */
