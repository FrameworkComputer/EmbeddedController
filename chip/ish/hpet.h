/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_HPET_H
#define __CROS_EC_HPET_H

#include "common.h"

/* ISH HPET config and timer registers */

#define TIMER0_CONF_CAP_REG 0x100
#define TIMER0_COMP_VAL_REG 0x108

/* HPET_GENERAL_CONFIG settings  */
#define HPET_GENERAL_CONFIG REG32(ISH_HPET_BASE + 0x10)
#define HPET_ENABLE_CNF BIT(0)
#define HPET_LEGACY_RT_CNF BIT(1)

/* Interrupt status acknowledge register */
#define HPET_INTR_CLEAR REG32(ISH_HPET_BASE + 0x20)

/* Main counter register. 64-bit */
#define HPET_MAIN_COUNTER_64 REG64(ISH_HPET_BASE + 0xF0)
#define HPET_MAIN_COUNTER_64_LO REG32(ISH_HPET_BASE + 0xF0)
#define HPET_MAIN_COUNTER_64_HI REG32(ISH_HPET_BASE + 0xF4)

/* HPET Timer 0/1/2 configuration*/
#define HPET_TIMER_CONF_CAP(x) REG32(ISH_HPET_BASE + 0x100 + ((x) * 0x20))
#define HPET_Tn_INT_TYPE_CNF BIT(1)
#define HPET_Tn_INT_ENB_CNF BIT(2)
#define HPET_Tn_TYPE_CNF BIT(3)
#define HPET_Tn_VAL_SET_CNF BIT(6)
#define HPET_Tn_32MODE_CNF BIT(8)
#define HPET_Tn_INT_ROUTE_CNF_SHIFT 0x9
#define HPET_Tn_INT_ROUTE_CNF_MASK (0x1f << 9)

/*
 * HPET Timer 0/1/2 comparator values. 1/2 are always 32-bit. 0 can be
 * configured as 64-bit.
 */
#define HPET_TIMER_COMP(x) REG32(ISH_HPET_BASE + 0x108 + ((x) * 0x20))
#define HPET_TIMER0_COMP_64 REG64(ISH_HPET_BASE + 0x108)

/* ISH 4/5: Special status register
 * Use this register to see HPET timer are settled after a write.
 */
#define HPET_CTRL_STATUS REG32(ISH_HPET_BASE + 0x160)
#define HPET_INT_STATUS_SETTLING BIT(1)
#define HPET_MAIN_COUNTER_SETTLING (BIT(2) | BIT(3))
#define HPET_T0_CAP_SETTLING BIT(4)
#define HPET_T1_CAP_SETTLING BIT(5)
#define HPET_T0_CMP_SETTLING (BIT(7) | BIT(8))
#define HPET_T1_CMP_SETTLING BIT(9)
#define HPET_MAIN_COUNTER_VALID BIT(13)
#define HPET_T1_SETTLING (HPET_T1_CAP_SETTLING | HPET_T1_CMP_SETTLING)
#define HPET_T0_SETTLING (HPET_T0_CAP_SETTLING | HPET_T0_CMP_SETTLING)
#define HPET_ANY_SETTLING (BIT(12) - 1)

#if defined(CHIP_FAMILY_ISH3)
#define ISH_HPET_CLK_FREQ 12000000 /* 12 MHz clock */
#elif defined(CHIP_FAMILY_ISH4) || defined(CHIP_FAMILY_ISH5)
#define ISH_HPET_CLK_FREQ 32768 /* 32.768 KHz clock */
#endif

#endif /* __CROS_EC_HPET_H */
