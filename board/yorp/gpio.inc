/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */
GPIO_INT(LID_OPEN,	 PIN(D, 2), GPIO_INT_BOTH |
				    GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(WP_L,		 PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)		/* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L, PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)	/* EC_PWR_BTN_ODL */

#ifdef CONFIG_POWER_S0IX
GPIO_INT(PCH_SLP_S0_L,	 PIN(A, 4), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S0_L */
#endif
GPIO_INT(PCH_SLP_S4_L,	 PIN(A, 3), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S4_L */
GPIO_INT(PCH_SLP_S3_L,	 PIN(A, 6), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S3_L */
GPIO_INT(SUSPWRDNACK,	 PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)	/* SUSPWRDNACK */
GPIO_INT(RSMRST_L_PGOOD, PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_RSMRST_ODL */
GPIO_INT(ALL_SYS_PGOOD,	 PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_PWROK_OD */
GPIO_INT(AC_PRESENT, 	 PIN(0, 0), GPIO_INT_BOTH, extpower_interrupt)		/* ACOK_OD */

/* Define PCH_SLP_S0_L after all interrupts if CONFIG_POWER_S0IX not defined. */
#ifndef CONFIG_POWER_S0IX
GPIO(PCH_SLP_S0_L,	 PIN(A, 4), GPIO_INPUT)		/* SLP_S0_L */
#endif

/*
 * TODO(b/74123961): Move PLT_RST_L and PCH_RCIN_L to virtual wires over eSPI
 */
GPIO(PLT_RST_L,		 PIN(C, 7), GPIO_INPUT)		/* Platform Reset from SoC */
GPIO(PCH_RCIN_L,	 PIN(0, 2), GPIO_ODR_HIGH)	/* SYS_RST_ODL */

GPIO(ENTERING_RW,	 PIN(8, 0), GPIO_OUT_LOW)	/* EC_ENTERING_RW */
GPIO(PCH_WAKE_L,	 PIN(7, 4), GPIO_ODR_HIGH)	/* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L,	 PIN(C, 1), GPIO_ODR_HIGH)	/* GPIO_PCH_PWRBTN_L */

GPIO(EN_PP5000,		 PIN(7, 3), GPIO_OUT_LOW)	/* EN_PP5000_A */
GPIO(PP5000_PG,		 PIN(C, 0), GPIO_INPUT)		/* PP5000_PG_OD */
GPIO(EN_PP3300,		 PIN(D, 4), GPIO_OUT_LOW)	/* EN_PP3300_A */
GPIO(PP3300_PG,		 PIN(6, 0), GPIO_INPUT)		/* PP3300_PG_OD */
GPIO(PMIC_EN,		 PIN(D, 7), GPIO_OUT_LOW)	/* Enable A Rails via PMIC */
GPIO(PCH_RSMRST_L,	 PIN(C, 2), GPIO_OUT_LOW)	/* RSMRST# to SOC. All _A rails now up. */
GPIO(PCH_SYS_PWROK,	 PIN(B, 7), GPIO_OUT_LOW)	/* EC_PCH_PWROK. All S0 rails now up. */

/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,	 PIN(3, 7), GPIO_INPUT | GPIO_SEL_1P8V)	/* PCH_PROCHOT_ODL */