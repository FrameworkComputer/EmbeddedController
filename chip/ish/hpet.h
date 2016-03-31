/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_HPET_H
#define __CROS_EC_HPET_H

#include "common.h"

/* ISH HPET config and timer registers */
#define GENERAL_CAPS_ID_REG		0x00
#define GENERAL_CONFIG_REG		0x10
#define GENERAL_INT_STAT_REG		0x20
#define MAIN_COUNTER_REG		0xF0

#define TIMER0_CONF_CAP_REG		0x100
#define TIMER0_COMP_VAL_REG		0x108
#define TIMER0_FSB_IR_REG		0x110

#define TIMER1_CONF_CAP_REG		0x120
#define TIMER1_COMP_VAL_REG		0x128
#define TIMER1_FSB_IR_REG		0x130

#define TIMER2_CONF_CAP_REG		0x140
#define TIMER2_COMP_VAL_REG		0x148
#define TIMER2_FSB_IR_REG		0x150

#define CONTROL_AND_STATUS_REG		0x160

#define HPET_ENABLE_CNF			(1<<0)
#define HPET_LEGACY_RT_CNF		(1<<1)
#define HPET_Tn_INT_TYPE_CNF            (1<<1)
#define HPET_Tn_INT_ENB_CNF		(1<<2)
#define HPET_Tn_TYPE_CNF		(1<<3)
#define HPET_Tn_32MODE_CNF		(1<<8)
#define HPET_Tn_INT_ROUTE_CNF_SHIFT	0x9
#define HPET_Tn_INT_ROUTE_CNF_MASK	(0x1f << 9)
#define HPET_GEN_CONF_STATUS_BIT	0x8
#define HPET_T0_CONF_CAP_BIT		0x4

#define HPET_GENERAL_CONFIG	REG32(ISH_HPET_BASE + GENERAL_CONFIG_REG)
#define HPET_MAIN_COUNTER	REG32(ISH_HPET_BASE + MAIN_COUNTER_REG)
#define HPET_INTR_CLEAR		REG32(ISH_HPET_BASE + GENERAL_INT_STAT_REG)
#define HPET_CTRL_STATUS	REG32(ISH_HPET_BASE + CONTROL_AND_STATUS_REG)

#define HPET_TIMER_CONF_CAP(x) \
	REG32(ISH_HPET_BASE + TIMER0_CONF_CAP_REG + (x * 0x20))
#define HPET_TIMER_COMP(x) \
	REG32(ISH_HPET_BASE + TIMER0_COMP_VAL_REG + (x * 0x20))

#if defined CONFIG_ISH_20
#define ISH_HPET_CLK_FREQ		1000000		/* 1 MHz clock */

#elif defined CONFIG_ISH_30
#define ISH_HPET_CLK_FREQ		12000000	/* 12 MHz clock */

#elif defined CONFIG_ISH_40
#define ISH_HPET_CLK_FREQ		32768		/* 32.768 KHz clock */
#endif

/* HPET timer 0 period of 10ms (100 ticks per second) */
#define ISH_TICKS_PER_SEC		100

#endif /* __CROS_EC_HPET_H */
