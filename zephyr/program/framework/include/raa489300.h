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

#endif	/* __CROS_EC_RAA489300_H */
