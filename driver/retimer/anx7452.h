/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7452: 2-Lane USB4 Retimer MUX driver
 */

#ifndef __CROS_EC_USB_RETIMER_ANX7452_H
#define __CROS_EC_USB_RETIMER_ANX7452_H

/*
 * Programming guide specifies it may be as much as 30-50 ms after chip power on
 * before it's ready for i2c
 */
#define ANX7452_I2C_WAKE_TIMEOUT_MS 30
#define ANX7452_I2C_WAKE_RETRY_DELAY_US 3000

/*
 * CTLTOP I2C address (7 bit)
 */
#define ANX7452_I2C_ADDR_CTLTOP_FLAGS 0x20

/*
 * TOP Status register
 *
 * 7   EN (0: Config info from pins, 1: Config info from registers)
 * 6   Reserved
 * 5   SWAP (0: host side, 1: device side)
 * 4   FLIP info (Read only use)
 * 3   USB4 info (Read only use)
 * 2   TBT info (Read only use)
 * 1   DP info (Read only use)
 * 0   USB3 info (Read only use)
 */
#define ANX7452_TOP_STATUS_REG 0xF8
#define ANX7452_TOP_REG_EN BIT(7)
#define ANX7452_TOP_SWAP_EN BIT(5)
#define ANX7452_TOP_FLIP_INFO BIT(4)
#define ANX7452_TOP_USB4_INFO BIT(3)
#define ANX7452_TOP_TBT_INFO BIT(2)
#define ANX7452_TOP_DP_INFO BIT(1)
#define ANX7452_TOP_USB3_INFO BIT(0)

/*
 * CTLTOP - 0 register
 *
 * 5   USB3 info (To set Bit 0 of TOP Status register indirectly)
 * 1   FLIP info (To set BIT 4 of TOP Status register indirectly)
 */
#define ANX7452_CTLTOP_CFG0_REG 0x04
#define ANX7452_CTLTOP_CFG0_USB3_EN BIT(5)
#define ANX7452_CTLTOP_CFG0_FLIP_EN BIT(1)
#define ANX7452_CTLTOP_CFG0_REG_BIT_MASK \
	(ANX7452_CTLTOP_CFG0_USB3_EN | ANX7452_CTLTOP_CFG0_FLIP_EN)

/*
 * CTLTOP - 1 register
 *
 * 0   DP info (To set Bit 1 of TOP Status register indirectly)
 */
#define ANX7452_CTLTOP_CFG1_REG 0x05
#define ANX7452_CTLTOP_CFG1_DP_EN BIT(0)
#define ANX7452_CTLTOP_CFG1_REG_BIT_MASK ANX7452_CTLTOP_CFG1_DP_EN

/*
 * CTLTOP - 2 register
 *
 * 7   USB4 info (To set Bit 3 of TOP Status register indirectly)
 * 0   TBT info (To set BIT 2 of TOP Status register indirectly)
 */
#define ANX7452_CTLTOP_CFG2_REG 0x06
#define ANX7452_CTLTOP_CFG2_USB4_EN BIT(7)
#define ANX7452_CTLTOP_CFG2_TBT_EN BIT(0)
#define ANX7452_CTLTOP_CFG2_REG_BIT_MASK \
	(ANX7452_CTLTOP_CFG2_USB4_EN | ANX7452_CTLTOP_CFG2_TBT_EN)
#endif /* __CROS_EC_USB_RETIMER_ANX7452_H */
