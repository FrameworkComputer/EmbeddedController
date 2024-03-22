/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kinetic KTU1125 Type-C Power Path Controller */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_KTU1125_H
#define __CROS_EC_KTU1125_H

#include "common.h"
#include "driver/ppc/ktu1125_public.h"

#define KTU1125_ID 0x0
#define KTU1125_CTRL_SW_CFG 0x1
#define KTU1125_SET_SW_CFG 0x2
#define KTU1125_SET_SW2_CFG 0x3
#define KTU1125_MONITOR_SNK 0x4
#define KTU1125_MONITOR_SRC 0x5
#define KTU1125_MONITOR_DATA 0x6
#define KTU1125_INTMASK_SNK 0x7
#define KTU1125_INTMASK_SRC 0x8
#define KTU1125_INTMASK_DATA 0x9
#define KTU1125_INT_SNK 0xA
#define KTU1125_INT_SRC 0xB
#define KTU1125_INT_DATA 0xC

/* KTU1125_ID default value */
#define KTU1125_VENDOR_DIE_IDS 0xA5

/* KTU1125_CTRL_SW_CFG bits */
#define KTU1125_SBU_SHUT BIT(0)
#define KTU1125_VCONN_EN BIT(1)
#define KTU1125_CC2S_VCONN BIT(2)
#define KTU1125_CC1S_VCONN BIT(3)
#define KTU1125_POW_MODE BIT(4)
#define KTU1125_SW_AB_EN BIT(5)
#define KTU1125_FRS_EN BIT(6)
#define KTU1125_EN_L BIT(7)

/* KTU1125_SET_SW_CFG bits and fields */
#define KTU1125_RDB_DIS BIT(0)
#define KTU1125_SS_CLP_SNK BIT(1)
#define KTU1125_TDON BIT(2)
#define KTU1125_VCONN_CLP_SHIFT 3
#define KTU1125_VCONN_CLP_LEN 2
#define KTU1125_SYSB_CLP_SHIFT 5
#define KTU1125_SYSB_CLP_LEN 3

/* VBUS Switch Current Limit Settings - SYSB_CLP */
#define KTU1125_SYSB_ILIM_0_6 0
#define KTU1125_SYSB_ILIM_1_05 1
#define KTU1125_SYSB_ILIM_1_70 2
#define KTU1125_SYSB_ILIM_3_30 3
#define KTU1125_SYSB_ILIM_3_60 4

/* VCONN Current Limit Settings - VCONN_CLP */
#define KTU1125_VCONN_ILIM_0_40 0
#define KTU1125_VCONN_ILIM_0_60 1
#define KTU1125_VCONN_ILIM_1_00 2
#define KTU1125_VCONN_ILIM_1_40 3

/* KTU1125_SET_SW2_CFG bits and fields */
#define KTU1125_OVP_BUS_SHIFT 0
#define KTU1125_OVP_BUS_LEN 3
#define KTU1125_DIS_RES_SHIFT 3
#define KTU1125_DIS_RES_LEN 2
#define KTU1125_VBUS_DIS_EN BIT(5)
#define KTU1125_T_HIC_SHIFT 6
#define KTU1125_T_HIC_LEN 2

/* VBUS Over Voltage Protection */
#define KTU1125_SYSB_VLIM_25_00 0
#define KTU1125_SYSB_VLIM_17_00 4
#define KTU1125_SYSB_VLIM_13_75 5
#define KTU1125_SYSB_VLIM_10_60 6
#define KTU1125_SYSB_VLIM_6_00 7

/* Discharge resistor [ohms] */
#define KTU1125_DIS_RES_1400 0
#define KTU1125_DIS_RES_730 1
#define KTU1125_DIS_RES_570 2
#define KTU1125_DIS_RES_205 3

/* T _HIC values [ms] */
#define KTU_T_HIC_MS_17 0
#define KTU_T_HIC_MS_34 1
#define KTU_T_HIC_MS_51 2
#define KTU_T_HIC_MS_68 3

/* Bits for MONITOR/INTMASK/INT SNK */
#define KTU1125_SS_FAIL BIT(0)
#define KTU1125_OTP BIT(1)
#define KTU1125_FR_SWAP BIT(2)
#define KTU1125_SYSA_SCP BIT(3)
#define KTU1125_SYSA_OCP BIT(4)
#define KTU1125_VBUS_OVP BIT(5)
#define KTU1125_VBUS_UVLO BIT(6)
#define KTU1125_SYSA_OK BIT(7)
#define KTU1125_SNK_MASK_ALL 0xFF

/* Bits for MONITOR/INTMASK/INT SRC */
#define KTU1125_VCONN_SCP BIT(0)
#define KTU1125_VCONN_CLP BIT(1)
#define KTU1125_VCONN_UVLO BIT(2)
#define KTU1125_SYSB_SCP BIT(3)
#define KTU1125_SYSB_OCP BIT(4)
#define KTU1125_SYSB_CLP BIT(5)
#define KTU1125_SYSB_UVLO BIT(6)
#define KTU1125_VBUS_OK BIT(7)
#define KTU1125_SRC_MASK_ALL 0xFF

/* Bits for MONITOR/INTMASK/INT DATA */
#define KTU1125_SBUB BIT(0)
#define KTU1125_SBUA BIT(1)
#define KTU1125_SBU2_OVP BIT(2)
#define KTU1125_SBU1_OVP BIT(3)
#define KTU1125_CC2_OVP BIT(4)
#define KTU1125_CC1_OVP BIT(5)
#define KTU1125_CC2S_CLAMP BIT(6)
#define KTU1125_CC1S_CLAMP BIT(7)
#define KTU1125_DATA_MASK_ALL 0xFC

#endif /* defined(__CROS_EC_KTU1125_H) */
