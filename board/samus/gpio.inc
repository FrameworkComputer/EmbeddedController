/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
/* Power button */
GPIO(POWER_BUTTON_L, A, 2, GPIO_INT_BOTH, power_button_interrupt)
/* Lid switch */
GPIO(LID_OPEN,       A, 3, GPIO_INT_BOTH, lid_interrupt)
/* AC power present */
GPIO(AC_PRESENT,     H, 3, GPIO_INT_BOTH, extpower_interrupt)
/* SLP_S0# signal from PCH */
GPIO(PCH_SLP_S0_L,   G, 6, GPIO_INT_BOTH, power_signal_interrupt)
/* SLP_S3# signal from PCH */
GPIO(PCH_SLP_S3_L,   G, 7, GPIO_INT_BOTH, power_signal_interrupt)
/* SLP_S5# signal from PCH */
GPIO(PCH_SLP_S5_L,   H, 1, GPIO_INT_BOTH, power_signal_interrupt)
/* SLP_SUS# signal from PCH */
GPIO(PCH_SLP_SUS_L,  G, 3, GPIO_INT_BOTH, power_signal_interrupt)
/* SUSWARN# signal from PCH */
GPIO(PCH_SUSWARN_L,  G, 2, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on 1.05V */
GPIO(PP1050_PGOOD,   H, 4, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on 1.2V (DRAM) */
GPIO(PP1200_PGOOD,   H, 6, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on 1.8V (DRAM) */
GPIO(PP1800_PGOOD,   L, 7, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on core VR */
GPIO(VCORE_PGOOD,    C, 6, GPIO_INT_BOTH, power_signal_interrupt)
/* Recovery signal from servo */
GPIO(RECOVERY_L,     A, 5, GPIO_PULL_UP | GPIO_INT_BOTH, switch_interrupt)
/* Write protect input */
GPIO(WP_L,           A, 4, GPIO_INT_BOTH,   switch_interrupt)
/* PCH backlight input */
GPIO(PCH_BL_EN,      M, 3, GPIO_INT_RISING, backlight_interrupt)
/* JTAG clock input */
GPIO(JTAG_TCK,       C, 0, GPIO_DEFAULT,    jtag_interrupt)
/* UART0 RX input */
GPIO(UART0_RX,       A, 0, GPIO_PULL_UP | GPIO_INT_BOTH_DSLEEP,
     uart_deepsleep_interrupt)

/*
 * Combined accelerometer input. This will become an interrupt, once we have
 * support for it.
 */
GPIO(ACCEL_INT, F, 7, GPIO_INPUT, NULL)

/* Capsense interrupt input. This connection exists but may not be configured */
#ifdef CONFIG_CAPSENSE
GPIO(CAPSENSE_INT_L, N, 0, GPIO_INT_FALLING, capsense_interrupt)
#else
GPIO(CAPSENSE_INT_L, N, 0, GPIO_INPUT, NULL)
#endif

/* Interrupt signal from PD MCU */
GPIO(PD_MCU_INT_L,   J, 5, GPIO_PULL_UP | GPIO_INT_FALLING | GPIO_INT_DSLEEP,
     pd_mcu_interrupt)

/* Other inputs */
/* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION1,       Q, 7, GPIO_INPUT, NULL)
/* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION2,       Q, 6, GPIO_INPUT, NULL)
/* Board version stuffing resistor 3 */
GPIO(BOARD_VERSION3,       Q, 5, GPIO_INPUT, NULL)
/* USB port overcurrent warning */
GPIO(USB1_OC_L,            E, 7, GPIO_INPUT, NULL)
/* USB charger port 1 status output */
GPIO(USB1_STATUS_L,        E, 6, GPIO_INPUT, NULL)
/* USB port overcurrent warning */
GPIO(USB2_OC_L,            E, 0, GPIO_INPUT, NULL)
/* USB charger port 2 status output */
GPIO(USB2_STATUS_L,        D, 7, GPIO_INPUT, NULL)

/* Outputs; all unasserted by default except for reset signals */
/* Force CPU to think it's overheated */
GPIO(CPU_PROCHOT,          B, 1, GPIO_OUT_LOW,  NULL)
/* Enable 1.20V supply */
GPIO(PP1200_EN,            H, 5, GPIO_OUT_LOW,  NULL)
/* Enable 3.3V DSW rail */
GPIO(PP3300_DSW_EN,        F, 6, GPIO_OUT_LOW,  NULL)
/* Enable 3.3V Gated DSW and core VDD */
GPIO(PP3300_DSW_GATED_EN,  J, 3, GPIO_OUT_LOW,  NULL)
/* Enable LTE radio */
GPIO(PP3300_LTE_EN,        D, 2, GPIO_OUT_LOW,  NULL)
/* Enable WiFi power */
GPIO(PP3300_WLAN_EN,       J, 0, GPIO_OUT_LOW,  NULL)
/* Enable Accelerometer power */
GPIO(PP3300_ACCEL_EN,      J, 1, GPIO_OUT_HIGH, NULL)
/* Enable 1.05V regulator */
GPIO(PP1050_EN,            C, 7, GPIO_OUT_LOW,  NULL)
/* Enable USB power */
GPIO(PP5000_USB_EN,        C, 5, GPIO_OUT_LOW,  NULL)
/* Enable 5V supply */
GPIO(PP5000_EN,            H, 7, GPIO_OUT_LOW,  NULL)
/* Enable 1.8V supply */
GPIO(PP1800_EN,            L, 6, GPIO_OUT_LOW,  NULL)
/* EC thinks everything is up and ready */
GPIO(SYS_PWROK,            H, 2, GPIO_OUT_LOW,  NULL)
/* Disable WiFi radio */
GPIO(WLAN_OFF_L,           J, 4, GPIO_OUT_LOW,  NULL)
/* USB PD MCU reset */
GPIO(USB_MCU_RST_L,        B, 0, GPIO_ODR_HIGH, NULL)
/* Enable backlight power */
GPIO(ENABLE_BACKLIGHT,     M, 7, GPIO_OUT_LOW,  NULL)
/* Enable touchpad power */
GPIO(ENABLE_TOUCHPAD,      N, 1, GPIO_OUT_LOW,  NULL)
/* Indicate when EC is entering RW code */
GPIO(ENTERING_RW,          D, 3, GPIO_OUT_LOW,  NULL)
/* Reset lightbar controllers */
GPIO(LIGHTBAR_RESET_L,     J, 2, GPIO_ODR_LOW,  NULL)
/* Indicate when VccDSW is good */
GPIO(PCH_DPWROK,           G, 0, GPIO_OUT_LOW,  NULL)
/* Reset PCH resume power plane logic */
GPIO(PCH_RSMRST_L,         C, 4, GPIO_OUT_LOW,  NULL)
/*
 * HDA_SDO is technically an output, but we need to leave it as an
 * input until we drive it high.  So can't use open-drain (HI_Z).
 */
/* HDA_SDO signal to PCH; when high, ME
 * ignores security descriptor */
GPIO(PCH_HDA_SDO,          G, 1, GPIO_INPUT,    NULL)
/* Wake signal from EC to PCH */
GPIO(PCH_WAKE_L,           F, 0, GPIO_ODR_HIGH, NULL)
/* Non-maskable interrupt pin to PCH */
GPIO(PCH_NMI_L,            F, 2, GPIO_ODR_HIGH, NULL)
/* Power button output to PCH */
GPIO(PCH_PWRBTN_L,         H, 0, GPIO_ODR_HIGH, NULL)
/* PWROK / APWROK signals to PCH */
GPIO(PCH_PWROK,            F, 5, GPIO_OUT_LOW,  NULL)
/* RCIN# line to PCH (for 8042 emulation) */
GPIO(PCH_RCIN_L,           F, 3, GPIO_ODR_HIGH, NULL)
/* Reset PCH resume power plane logic */
GPIO(PCH_SYS_RST_L,        F, 1, GPIO_ODR_HIGH, NULL)
/* System management interrupt to PCH */
GPIO(PCH_SMI_L,            F, 4, GPIO_ODR_HIGH, NULL)
/* Reset touch screen */
GPIO(TOUCHSCREEN_RESET_L,  N, 7, GPIO_ODR_LOW,  NULL)
/* AC present signal buffered to PCH */
GPIO(PCH_ACOK,             M, 6, GPIO_OUT_LOW,  NULL)
#ifndef HEY_USE_BUILTIN_CLKRUN
/* Dunno. Probably important, though. */
GPIO(LPC_CLKRUN_L,         M, 2, GPIO_ODR_HIGH, NULL)
#endif
/* USB charger port 1 CTL1 output */
GPIO(USB1_CTL1,            E, 1, GPIO_OUT_LOW,  NULL)
/* USB charger port 1 CTL2 output */
GPIO(USB1_CTL2,            E, 2, GPIO_OUT_HIGH, NULL)
/* USB charger port 1 CTL3 output */
GPIO(USB1_CTL3,            E, 3, GPIO_OUT_LOW,  NULL)
/* USB charger port 1 enable */
GPIO(USB1_ENABLE,          E, 4, GPIO_OUT_HIGH, NULL)
/* USB charger port 1 ILIM_SEL output */
GPIO(USB1_ILIM_SEL,        E, 5, GPIO_OUT_LOW,  NULL)
/* USB charger port 2 CTL1 output */
GPIO(USB2_CTL1,            D, 0, GPIO_OUT_LOW,  NULL)
/* USB charger port 2 CTL2 output */
GPIO(USB2_CTL2,            D, 1, GPIO_OUT_HIGH, NULL)
/* USB charger port 2 CTL3 output */
GPIO(USB2_CTL3,            D, 4, GPIO_OUT_LOW,  NULL)
/* USB charger port 2 enable */
GPIO(USB2_ENABLE,          D, 5, GPIO_OUT_HIGH, NULL)
/* USB charger port 2 ILIM_SEL output */
GPIO(USB2_ILIM_SEL,        D, 6, GPIO_OUT_LOW,  NULL)
