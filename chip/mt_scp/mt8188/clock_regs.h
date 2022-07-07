/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SCP clock module registers */

#ifndef __CROS_EC_CLOCK_REGS_H
#define __CROS_EC_CLOCK_REGS_H

/* clock source select */
#define SCP_CLK_SW_SEL REG32(SCP_CLK_CTRL_BASE + 0x0000)
#define CLK_SW_SEL_SYSTEM 0
#define CLK_SW_SEL_32K 1
#define CLK_SW_SEL_ULPOSC2 2
#define CLK_SW_SEL_ULPOSC1 3
#define SCP_CLK_ENABLE REG32(SCP_CLK_CTRL_BASE + 0x0004)
#define CLK_HIGH_EN BIT(1) /* ULPOSC */
#define CLK_HIGH_CG BIT(2)
#define SCP_CLK_SAFE_ACK REG32(SCP_CLK_CTRL_BASE + 0x0008)
#define CLK_SAFE_ACK_SYS BIT(0)
#define CLK_SAFE_ACK_HIGH BIT(1) /* ULPOSC */
/* clock general control */
#define SCP_CLK_CTRL_GENERAL_CTRL REG32(SCP_CLK_CTRL_BASE + 0x009C)
#define VREQ_PMIC_WRAP_SEL (0x3)

/* TOPCK clk */
#define TOPCK_BASE AP_REG_BASE
#define AP_CLK_CFG_UPDATE2 REG32(TOPCK_BASE + 0x000C)
#define F_ULPOSC_CK_UPDATE BIT(25)
#define AP_CLK_CFG_22_SET REG32(TOPCK_BASE + 0x012C)
#define AP_CLK_CFG_22_CLR REG32(TOPCK_BASE + 0x0130)
#define ULPOSC_CLK_SEL (0x3 << 8)
#define PDN_F_ULPOSC_CK BIT(15)

#define AP_CLK_MISC_CFG_1 REG32(TOPCK_BASE + 0x0238)
#define F_ULPOSC_CORE_CK_EN BIT(17)

/* OSC meter */
#define AP_CLK_DBG_CFG REG32(TOPCK_BASE + 0x020C)
#define DBG_BIST_SOURCE_ULPOSC1 (0x2A << 8)
#define DBG_BIST_SOURCE_ULPOSC2 (0x2C << 8)
#define AP_CLK26CALI_0 REG32(TOPCK_BASE + 0x0218)
#define CFG_FREQ_METER_RUN BIT(4)
#define CFG_FREQ_METER_ENABLE BIT(7)
#define AP_CLK26CALI_1 REG32(TOPCK_BASE + 0x021C)
#define CFG_CKGEN_LOAD_CNT 0x01ff0000
#define CFG_FREQ_COUNTER(CFG1) ((CFG1) & 0xFFFF)
#define AP_CLK_MISC_CFG_0 REG32(TOPCK_BASE + 0x022C)
#define MISC_METER_DIVISOR_MASK 0xff000000
#define MISC_METER_DIV_1 0

/*
 * ULPOSC
 * osc: 0 for ULPOSC1, 1 for ULPOSC2.
 */
#define AP_ULPOSC_CON0_BASE (AP_REG_BASE + 0xC600)
#define AP_ULPOSC_CON1_BASE (AP_REG_BASE + 0xC604)
#define AP_ULPOSC_CON2_BASE (AP_REG_BASE + 0xC608)
#define AP_ULPOSC_CON0(osc) REG32(AP_ULPOSC_CON0_BASE + (osc) * 0x50)
#define AP_ULPOSC_CON1(osc) REG32(AP_ULPOSC_CON1_BASE + (osc) * 0x50)
#define AP_ULPOSC_CON2(osc) REG32(AP_ULPOSC_CON2_BASE + (osc) * 0x50)
/*
 * AP_ULPOSC_CON0
 * bit0-6: calibration
 * bit7-13: iband
 * bit14-17: fband
 * bit18-23: div
 * bit24: cp_en
 * bit25-31: reserved
 */
#define OSC_CALI_SHIFT 0
#define OSC_CALI_MASK 0x7f
#define OSC_IBAND_SHIFT 7
#define OSC_FBAND_SHIFT 14
#define OSC_DIV_SHIFT 18
#define OSC_CP_EN BIT(24)
/*
 * AP_ULPOSC_CON1
 * bit26: div2_en
 * bit24-25: mod
 * bit16-23: rsv2
 * bit8-15: rsv1
 * bit0-7: 32K calibration
 */
#define OSC_32KCALI_SHIFT 0
#define OSC_RSV1_SHIFT 8
#define OSC_RSV2_SHIFT 16
#define OSC_MOD_SHIFT 24
#define OSC_DIV2_EN BIT(26)

/*
 * AP_ULPOSC_CON2
 * bit0-7: bias
 */
#define OSC_BIAS_SHIFT 0

#endif /* __CROS_EC_CLOCK_REGS_H */
