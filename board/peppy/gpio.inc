/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


/* Inputs with interrupt handlers are first for efficiency */
/* Power button */
GPIO(POWER_BUTTON_L, A, 2, GPIO_INT_BOTH_DSLEEP, power_button_interrupt)
/* Lid switch */
GPIO(LID_OPEN,       A, 3, GPIO_INT_BOTH_DSLEEP, lid_interrupt)
/* AC power present */
GPIO(AC_PRESENT,     H, 3, GPIO_INT_BOTH_DSLEEP, extpower_interrupt)
/* Backlight enable signal from PCH */
GPIO(PCH_BKLTEN,     M, 3, GPIO_INT_BOTH,        backlight_interrupt)
/* SLP_S0# signal from PCH */
GPIO(PCH_SLP_S0_L,   G, 6, GPIO_INT_BOTH,        power_signal_interrupt)
/* SLP_S3# signal from PCH */
GPIO(PCH_SLP_S3_L,   G, 7, GPIO_INT_BOTH_DSLEEP, power_signal_interrupt)
/* SLP_S5# signal from PCH */
GPIO(PCH_SLP_S5_L,   H, 1, GPIO_INT_BOTH_DSLEEP, power_signal_interrupt)
/* SLP_SUS# signal from PCH */
GPIO(PCH_SLP_SUS_L,  G, 3, GPIO_INT_BOTH,        power_signal_interrupt)
/* Power good on 1.05V */
GPIO(PP1050_PGOOD,   H, 4, GPIO_INT_BOTH,        power_signal_interrupt)
/* Power good on 1.35V (DRAM) */
GPIO(PP1350_PGOOD,   H, 6, GPIO_INT_BOTH,        power_signal_interrupt)
/* Power good on 5V */
GPIO(PP5000_PGOOD,   N, 0, GPIO_INT_BOTH,        power_signal_interrupt)
/* Power good on core VR */
GPIO(VCORE_PGOOD,    C, 6, GPIO_INT_BOTH,        power_signal_interrupt)
/* PCH wants EDP enabled */
GPIO(PCH_EDP_VDD_EN, J, 1, GPIO_INT_BOTH,        power_interrupt)
/* Recovery signal from servo */
GPIO(RECOVERY_L,     A, 5, GPIO_PULL_UP | GPIO_INT_BOTH, switch_interrupt)
/* Write protect input */
GPIO(WP_L,           A, 4, GPIO_INT_BOTH,        switch_interrupt)
/* JTAG clock input */
GPIO(JTAG_TCK,       C, 0, GPIO_DEFAULT,         jtag_interrupt)
/* UART0 RX input */
GPIO(UART0_RX,       A, 0, GPIO_PULL_UP | GPIO_INT_BOTH_DSLEEP,
     uart_deepsleep_interrupt)

/* Other inputs */
/* From thermal sensor */
GPIO(FAN_ALERT_L,          B, 0, GPIO_INPUT,    NULL)
/* SUSWARN# signal from PCH */
GPIO(PCH_SUSWARN_L,        G, 2, GPIO_INT_BOTH, NULL)
/* USB port overcurrent warning */
GPIO(USB1_OC_L,            E, 7, GPIO_INPUT,    NULL)
/* USB port overcurrent warning */
GPIO(USB2_OC_L,            E, 0, GPIO_INPUT,    NULL)
/* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION1,       Q, 5, GPIO_INPUT,    NULL)
/* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION2,       Q, 6, GPIO_INPUT,    NULL)
/* Board version stuffing resistor 3 */
GPIO(BOARD_VERSION3,       Q, 7, GPIO_INPUT,    NULL)
/* Power good to the CPU */
GPIO(CPU_PGOOD,            C, 4, GPIO_INPUT,    NULL)
/* Battery present. Repurposed BAT_TEMP */
GPIO(BAT_PRESENT_L,        B, 4, GPIO_INPUT,    NULL)

/* Outputs; all unasserted by default except for reset signals */
/* Force CPU to think it's overheated */
GPIO(CPU_PROCHOT,          B, 1, GPIO_OUT_LOW, NULL)
/* Enable 1.35V supply */
GPIO(PP1350_EN,            H, 5, GPIO_OUT_LOW, NULL)
/* Enable power to lots of peripherals */
GPIO(PP3300_DX_EN,         J, 2, GPIO_OUT_LOW, NULL)
/* Enable LTE radio */
GPIO(PP3300_LTE_EN,        D, 2, GPIO_OUT_LOW, NULL)
/* Enable WiFi power */
GPIO(PP3300_WLAN_EN,       J, 0, GPIO_OUT_LOW, NULL)
/* Enable 1.05V regulator */
GPIO(SUSP_VR_EN,           C, 7, GPIO_OUT_LOW, NULL)
/* Stuffing option - not connected */
GPIO(VCORE_EN,             C, 5, GPIO_OUT_LOW, NULL)
/* Enable 5V supply */
GPIO(PP5000_EN,            H, 7, GPIO_OUT_LOW, NULL)
/* Enable fan power rail */
GPIO(PP5000_FAN_EN,        J, 3, GPIO_OUT_LOW, NULL)
/* EC thinks everything is up and ready */
GPIO(SYS_PWROK,            H, 2, GPIO_OUT_LOW, NULL)
/* Disable WiFi radio */
GPIO(WLAN_OFF_L,           J, 4, GPIO_OUT_LOW, NULL)
/* Allow battery to charge when on AC */
GPIO(CHARGE_L,             E, 6, GPIO_OUT_LOW, NULL)

/* Enable backlight power */
GPIO(ENABLE_BACKLIGHT,     M, 7, GPIO_OUT_LOW, NULL)
/* Enable touchpad power */
GPIO(ENABLE_TOUCHPAD,      N, 1, GPIO_OUT_LOW, NULL)
/* Indicate when EC is entering RW code */
GPIO(ENTERING_RW,          D, 3, GPIO_OUT_LOW, NULL)
/* Indicate when VccDSW is good */
GPIO(PCH_DPWROK,           G, 0, GPIO_OUT_LOW, NULL)
/*
 * HDA_SDO is technically an output, but we need to leave it as an
 * input until we drive it high.  So can't use open-drain (HI_Z).
 */
/* HDA_SDO signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_HDA_SDO,          G, 1, GPIO_INPUT,    NULL)
/* Wake signal from EC to PCH */
GPIO(PCH_WAKE_L,           F, 0, GPIO_OUT_HIGH, NULL)
/* Non-maskable interrupt pin to PCH */
GPIO(PCH_NMI_L,            F, 2, GPIO_OUT_HIGH, NULL)
/* Power button output to PCH */
GPIO(PCH_PWRBTN_L,         H, 0, GPIO_OUT_HIGH, NULL)
/* PWROK / APWROK signals to PCH */
GPIO(PCH_PWROK,            F, 5, GPIO_OUT_LOW,  NULL)
/*
 * PL6 is one of 4 pins on the EC which can't be used in open-drain
 * mode.  To work around this PCH_RCIN_L is set to an input. It will
 * only be set to an output when it needs to be driven to 0.
 */
/* RCIN# line to PCH (for 8042 emulation) */
GPIO(PCH_RCIN_L,           L, 6, GPIO_INPUT,    NULL)
/* Reset PCH resume power plane logic */
GPIO(PCH_RSMRST_L,         F, 1, GPIO_OUT_LOW,  NULL)
/* System management interrupt to PCH */
GPIO(PCH_SMI_L,            F, 4, GPIO_ODR_HIGH, NULL)
/* Reset touch screen */
GPIO(TOUCHSCREEN_RESET_L,  N, 7, GPIO_OUT_LOW,  NULL)
/* Enable EDP (passthru from PCH) */
GPIO(EC_EDP_VDD_EN,        J, 5, GPIO_OUT_LOW,  NULL)

/* Dunno. Probably important, though. */
GPIO(LPC_CLKRUN_L,         M, 2, GPIO_ODR_HIGH, NULL)
/* USB port 1 output power enable */
GPIO(USB1_ENABLE,          E, 4, GPIO_OUT_LOW,  NULL)
/* USB port 2 output power enable */
GPIO(USB2_ENABLE,          D, 5, GPIO_OUT_LOW,  NULL)

/* Acknowledge PCH SUSWARN# signal */
GPIO(PCH_SUSACK_L,         F, 3, GPIO_OUT_HIGH, NULL)
/* Not supposed to be here */
GPIO(PCH_RTCRST_L,         F, 6, GPIO_ODR_HIGH, NULL)
/* Not supposed to be here */
GPIO(PCH_SRTCRST_L,        F, 7, GPIO_ODR_HIGH, NULL)

GPIO(BAT_LED0_L, D, 0, GPIO_ODR_HIGH, NULL) /* Battery charging LED - blue */
GPIO(BAT_LED1_L, N, 4, GPIO_ODR_HIGH, NULL) /* Battery charging LED - orange */
GPIO(PWR_LED0_L, D, 1, GPIO_ODR_HIGH, NULL) /* Power LED - blue */
GPIO(PWR_LED1_L, N, 6, GPIO_ODR_HIGH, NULL) /* Power LED - orange */
