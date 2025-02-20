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

/*
 * OutputVoltage Register (0x15)
 */
#define PPS_VOLTAGE_STEP_MV 12
#define PPS_VOLTAGE_MAX 24564
#define AVS_VOLTAGE_STEP_MV 24
#define AVS_VOLTAGE_MAX 54912

int write_level_buck_registers(bool is_epr);

void board_level_buck_update(void);

void level_buck_set_input_current_limit(int ma);

#endif	/* __CROS_EC_RAA489300_H */
