/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @file
 *
 * @brief Backend API for SYV682X emulator
 */

#ifndef __EMUL_SYV682X_H
#define __EMUL_SYV682X_H

#include <drivers/i2c_emul.h>
#include <stdint.h>

/* Register info copied from syv682.h */

/* SYV682x register addresses */
#define SYV682X_STATUS_REG		0x00
#define SYV682X_CONTROL_1_REG		0x01
#define SYV682X_CONTROL_2_REG		0x02
#define SYV682X_CONTROL_3_REG		0x03
#define SYV682X_CONTROL_4_REG		0x04

/* Status Register */
#define SYV682X_STATUS_OC_HV    BIT(7)
#define SYV682X_STATUS_RVS      BIT(6)
#define SYV682X_STATUS_OC_5V    BIT(5)
#define SYV682X_STATUS_OVP      BIT(4)
#define SYV682X_STATUS_FRS      BIT(3)
#define SYV682X_STATUS_TSD      BIT(2)
#define SYV682X_STATUS_VSAFE_5V BIT(1)
#define SYV682X_STATUS_VSAFE_0V BIT(0)
#define SYV682X_STATUS_INT_MASK 0xfc
#define SYV682X_STATUS_NONE     0

/* Control Register 1 */
#define SYV682X_CONTROL_1_CH_SEL	BIT(1)
#define SYV682X_CONTROL_1_HV_DR		BIT(2)
#define SYV682X_CONTROL_1_PWR_ENB	BIT(7)

#define SYV682X_5V_ILIM_MASK		0x18
#define SYV682X_5V_ILIM_BIT_SHIFT	3
#define SYV682X_5V_ILIM_1_25		0
#define SYV682X_5V_ILIM_1_75		1
#define SYV682X_5V_ILIM_2_25		2
#define SYV682X_5V_ILIM_3_30		3

#define SYV682X_HV_ILIM_MASK		0x60
#define SYV682X_HV_ILIM_BIT_SHIFT	5
#define SYV682X_HV_ILIM_1_25		0
#define SYV682X_HV_ILIM_1_75		1
#define SYV682X_HV_ILIM_3_30		2
#define SYV682X_HV_ILIM_5_50		3

/* Control Register 2 */
#define SYV682X_OC_DELAY_MASK		GENMASK(7, 6)
#define SYV682X_OC_DELAY_SHIFT		6
#define SYV682X_OC_DELAY_1MS		0
#define SYV682X_OC_DELAY_10MS		1
#define SYV682X_OC_DELAY_50MS		2
#define SYV682X_OC_DELAY_100MS		3
#define SYV682X_DSG_TIME_MASK		GENMASK(5, 4)
#define SYV682X_DSG_TIME_SHIFT		4
#define SYV682X_DSG_TIME_50MS		0
#define SYV682X_DSG_TIME_100MS		1
#define SYV682X_DSG_TIME_200MS		2
#define SYV682X_DSG_TIME_400MS		3
#define SYV682X_DSG_RON_MASK		GENMASK(3, 2)
#define SYV682X_DSG_RON_SHIFT		2
#define SYV682X_DSG_RON_200_OHM		0
#define SYV682X_DSG_RON_400_OHM		1
#define SYV682X_DSG_RON_800_OHM		2
#define SYV682X_DSG_RON_1600_OHM	3
#define SYV682X_CONTROL_2_SDSG		BIT(1)
#define SYV682X_CONTROL_2_FDSG		BIT(0)

/* Control Register 3 */
#define SYV682X_BUSY			BIT(7)
#define SYV682X_RVS_MASK		BIT(3)
#define SYV682X_RST_REG			BIT(0)
#define SYV682X_OVP_MASK		0x70
#define SYV682X_OVP_BIT_SHIFT		4
#define SYV682X_OVP_06_0		0
#define SYV682X_OVP_08_0		1
#define SYV682X_OVP_11_1		2
#define SYV682X_OVP_12_1		3
#define SYV682X_OVP_14_2		4
#define SYV682X_OVP_17_9		5
#define SYV682X_OVP_21_6		6
#define SYV682X_OVP_23_7		7
#define SYV682X_CONTROL_3_NONE		0

/* Control Register 4 */
#define SYV682X_CONTROL_4_CC1_BPS	BIT(7)
#define SYV682X_CONTROL_4_CC2_BPS	BIT(6)
#define SYV682X_CONTROL_4_VCONN1	BIT(5)
#define SYV682X_CONTROL_4_VCONN2	BIT(4)
#define SYV682X_CONTROL_4_VBAT_OVP	BIT(3)
#define SYV682X_CONTROL_4_VCONN_OCP	BIT(2)
#define SYV682X_CONTROL_4_CC_FRS	BIT(1)
#define SYV682X_CONTROL_4_INT_MASK	0x0c
#define SYV682X_CONTROL_4_NONE      0

/**
 * @brief Get pointer to SYV682x emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to smart battery emulator
 */
struct i2c_emul *syv682x_emul_get(int ord);

/**
 * @brief Set the underlying interrupt conditions affecting the SYV682x
 *
 * @param emul      SYV682x emulator
 * @param status    A status register value corresponding to the underlying
 *                  conditions
 * @param control_4 A control 4 register value corresponding to the underlying
 *                  conditions; only the bits in SYV682X_CONTROL_4_INT_MASK have
 *                  an effect.
 */
void syv682x_emul_set_condition(struct i2c_emul *emul, uint8_t status,
		uint8_t control_4);

/**
 * @brief Cause CONTROL_3[BUSY] to be set for a number of reads. This bit
 *        signals that I2C writes will be ignored. Each call overrides any
 *        previous setting.
 *
 * @param emul  SYV682x emulator
 * @param reads The number of reads of CONTROL_3 to keep BUSY set for
 */
void syv682x_emul_set_busy_reads(struct i2c_emul *emul, int reads);

/**
 * @brief Set value of a register of SYV682x
 *
 * @param emul SYV682x emulator
 * @param reg  Register address
 * @param val  Value to write to the register
 *
 * @return 0 on success, error code on error
 */
int syv682x_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val);

/**
 * @brief Get value of a register of SYV682x
 *
 * @param emul SYV682x emulator
 * @param reg  Register address
 * @param val  Pointer at which to store current value of register
 *
 * @return 0 on success, error code on error
 */
int syv682x_emul_get_reg(struct i2c_emul *emul, int reg, uint8_t *val);

#endif /* __EMUL_SYV682X_H */
