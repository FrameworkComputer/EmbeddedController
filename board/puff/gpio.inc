/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* TODO: pmarheine - use the real values. */

/* Wake Source interrupts */
GPIO_INT(WP_L,                 PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)              /* EC_WP_ODL */
GPIO_INT(AC_PRESENT,		PIN(C, 2), GPIO_INT_BOTH,			extpower_interrupt)
GPIO_INT(POWER_BUTTON_L,	PIN(0, 4), GPIO_INT_BOTH | GPIO_PULL_UP,	power_button_interrupt)	/* MECH_PWR_BTN_ODL */

GPIO_INT(RSMRST_L_PGOOD,       PIN(E, 2), GPIO_INT_BOTH, intel_x86_rsmrst_signal_interrupt)
GPIO_INT(PCH_SLP_S0_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S3_L,             PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S4_L,             PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_ALL_SYS_PWRGD,  PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PP5000_A_PG_OD,       PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)

GPIO(SYS_RESET_L,              PIN(0, 2), GPIO_ODR_HIGH)        /* SYS_RST_ODL */
GPIO(PCH_WAKE_L,               PIN(7, 4), GPIO_ODR_HIGH)        /* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L,             PIN(C, 1), GPIO_ODR_HIGH)        /* EC_PCH_PWR_BTN_ODL */
GPIO(PCH_RSMRST_L,          PIN(A, 6), GPIO_OUT_LOW)
GPIO(ENTERING_RW,              PIN(E, 3), GPIO_OUT_LOW)         /* EC_ENTERING_RW */
GPIO(CPU_PROCHOT,           PIN(6, 3), GPIO_ODR_HIGH)
GPIO(EN_PP5000,           PIN(6, 4), GPIO_ODR_HIGH)
GPIO(EN_PP5000_A,           PIN(6, 5), GPIO_ODR_HIGH)
GPIO(PCH_SYS_PWROK,         PIN(3, 7), GPIO_OUT_LOW)
GPIO(EN_A_RAILS,               PIN(A, 3), GPIO_OUT_LOW)
