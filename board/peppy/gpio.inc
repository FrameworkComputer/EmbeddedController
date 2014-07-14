/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO(POWER_BUTTON_L, A, 2, GPIO_INT_BOTH_DSLEEP,                power_button_interrupt)   /* Power button */
GPIO(LID_OPEN,       A, 3, GPIO_INT_BOTH_DSLEEP,                lid_interrupt)            /* Lid switch */
GPIO(AC_PRESENT,     H, 3, GPIO_INT_BOTH_DSLEEP,                extpower_interrupt)       /* AC power present */
GPIO(PCH_BKLTEN,     M, 3, GPIO_INT_BOTH,                       backlight_interrupt)      /* Backlight enable signal from PCH */
GPIO(PCH_SLP_S0_L,   G, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_S0# signal from PCH */
GPIO(PCH_SLP_S3_L,   G, 7, GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S3# signal from PCH */
GPIO(PCH_SLP_S5_L,   H, 1, GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S5# signal from PCH */
GPIO(PCH_SLP_SUS_L,  G, 3, GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_SUS# signal from PCH */
GPIO(PP1050_PGOOD,   H, 4, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.05V */
GPIO(PP1350_PGOOD,   H, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.35V (DRAM) */
GPIO(PP5000_PGOOD,   N, 0, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 5V */
GPIO(VCORE_PGOOD,    C, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on core VR */
GPIO(PCH_EDP_VDD_EN, J, 1, GPIO_INT_BOTH,                       power_interrupt)          /* PCH wants EDP enabled */
GPIO(RECOVERY_L,     A, 5, GPIO_PULL_UP | GPIO_INT_BOTH,        switch_interrupt)         /* Recovery signal from servo */
GPIO(WP_L,           A, 4, GPIO_INT_BOTH,                       switch_interrupt)         /* Write protect input */
GPIO(JTAG_TCK,       C, 0, GPIO_DEFAULT,                        jtag_interrupt)           /* JTAG clock input */
GPIO(UART0_RX,       A, 0, GPIO_PULL_UP | GPIO_INT_BOTH_DSLEEP, uart_deepsleep_interrupt) /* UART0 RX input */

/* Other inputs */
GPIO(FAN_ALERT_L,          B, 0, GPIO_INPUT,    NULL) /* From thermal sensor */
GPIO(PCH_SUSWARN_L,        G, 2, GPIO_INT_BOTH, NULL) /* SUSWARN# signal from PCH */
GPIO(USB1_OC_L,            E, 7, GPIO_INPUT,    NULL) /* USB port overcurrent warning */
GPIO(USB2_OC_L,            E, 0, GPIO_INPUT,    NULL) /* USB port overcurrent warning */
GPIO(BOARD_VERSION1,       Q, 5, GPIO_INPUT,    NULL) /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2,       Q, 6, GPIO_INPUT,    NULL) /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3,       Q, 7, GPIO_INPUT,    NULL) /* Board version stuffing resistor 3 */
GPIO(CPU_PGOOD,            C, 4, GPIO_INPUT,    NULL) /* Power good to the CPU */
GPIO(BAT_PRESENT_L,        B, 4, GPIO_INPUT,    NULL) /* Battery present. Repurposed BAT_TEMP */

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,          B, 1, GPIO_OUT_LOW, NULL) /* Force CPU to think it's overheated */
GPIO(PP1350_EN,            H, 5, GPIO_OUT_LOW, NULL) /* Enable 1.35V supply */
GPIO(PP3300_DX_EN,         J, 2, GPIO_OUT_LOW, NULL) /* Enable power to lots of peripherals */
GPIO(PP3300_LTE_EN,        D, 2, GPIO_OUT_LOW, NULL) /* Enable LTE radio */
GPIO(PP3300_WLAN_EN,       J, 0, GPIO_OUT_LOW, NULL) /* Enable WiFi power */
GPIO(SUSP_VR_EN,           C, 7, GPIO_OUT_LOW, NULL) /* Enable 1.05V regulator */
GPIO(VCORE_EN,             C, 5, GPIO_OUT_LOW, NULL) /* Stuffing option - not connected */
GPIO(PP5000_EN,            H, 7, GPIO_OUT_LOW, NULL) /* Enable 5V supply */
GPIO(PP5000_FAN_EN,        J, 3, GPIO_OUT_LOW, NULL) /* Enable fan power rail */
GPIO(SYS_PWROK,            H, 2, GPIO_OUT_LOW, NULL) /* EC thinks everything is up and ready */
GPIO(WLAN_OFF_L,           J, 4, GPIO_OUT_LOW, NULL) /* Disable WiFi radio */
GPIO(CHARGE_L,             E, 6, GPIO_OUT_LOW, NULL) /* Allow battery to charge when on AC */

GPIO(ENABLE_BACKLIGHT,     M, 7, GPIO_OUT_LOW, NULL) /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,      N, 1, GPIO_OUT_LOW, NULL) /* Enable touchpad power */
GPIO(ENTERING_RW,          D, 3, GPIO_OUT_LOW, NULL) /* Indicate when EC is entering RW code */
GPIO(PCH_DPWROK,           G, 0, GPIO_OUT_LOW, NULL) /* Indicate when VccDSW is good */

/*
 * HDA_SDO is technically an output, but we need to leave it as an
 * input until we drive it high.  So can't use open-drain (HI_Z).
 */
GPIO(PCH_HDA_SDO,          G, 1, GPIO_INPUT,    NULL) /* HDA_SDO signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_WAKE_L,           F, 0, GPIO_OUT_HIGH, NULL) /* Wake signal from EC to PCH */
GPIO(PCH_NMI_L,            F, 2, GPIO_OUT_HIGH, NULL) /* Non-maskable interrupt pin to PCH */
GPIO(PCH_PWRBTN_L,         H, 0, GPIO_OUT_HIGH, NULL) /* Power button output to PCH */
GPIO(PCH_PWROK,            F, 5, GPIO_OUT_LOW,  NULL) /* PWROK / APWROK signals to PCH */

/*
 * PL6 is one of 4 pins on the EC which can't be used in open-drain
 * mode.  To work around this PCH_RCIN_L is set to an input. It will
 * only be set to an output when it needs to be driven to 0.
 */
GPIO(PCH_RCIN_L,           L, 6, GPIO_INPUT,    NULL) /* RCIN# line to PCH (for 8042 emulation) */
GPIO(PCH_RSMRST_L,         F, 1, GPIO_OUT_LOW,  NULL) /* Reset PCH resume power plane logic */
GPIO(PCH_SMI_L,            F, 4, GPIO_ODR_HIGH, NULL) /* System management interrupt to PCH */
GPIO(TOUCHSCREEN_RESET_L,  N, 7, GPIO_OUT_LOW,  NULL) /* Reset touch screen */
GPIO(EC_EDP_VDD_EN,        J, 5, GPIO_OUT_LOW,  NULL) /* Enable EDP (passthru from PCH) */

GPIO(LPC_CLKRUN_L,         M, 2, GPIO_ODR_HIGH, NULL) /* Dunno. Probably important, though. */
GPIO(USB1_ENABLE,          E, 4, GPIO_OUT_LOW,  NULL) /* USB port 1 output power enable */
GPIO(USB2_ENABLE,          D, 5, GPIO_OUT_LOW,  NULL) /* USB port 2 output power enable */

GPIO(PCH_SUSACK_L,         F, 3, GPIO_OUT_HIGH, NULL) /* Acknowledge PCH SUSWARN# signal */
GPIO(PCH_RTCRST_L,         F, 6, GPIO_ODR_HIGH, NULL) /* Not supposed to be here */
GPIO(PCH_SRTCRST_L,        F, 7, GPIO_ODR_HIGH, NULL) /* Not supposed to be here */

GPIO(BAT_LED0_L,           D, 0, GPIO_ODR_HIGH, NULL) /* Battery charging LED - blue */
GPIO(BAT_LED1_L,           N, 4, GPIO_ODR_HIGH, NULL) /* Battery charging LED - orange */
GPIO(PWR_LED0_L,           D, 1, GPIO_ODR_HIGH, NULL) /* Power LED - blue */
GPIO(PWR_LED1_L,           N, 6, GPIO_ODR_HIGH, NULL) /* Power LED - orange */

ALTERNATE(A, 0x03, 1,  MODULE_UART,    GPIO_PULL_UP)	/* UART0 */
ALTERNATE(B, 0x04, 3,  MODULE_I2C,     0)		/* I2C0 SCL */
ALTERNATE(B, 0x08, 3,  MODULE_I2C,     GPIO_OPEN_DRAIN)	/* I2C0 SDA */
ALTERNATE(B, 0x40, 3,  MODULE_I2C,     0)		/* I2C5 SCL */
ALTERNATE(B, 0x80, 3,  MODULE_I2C,     GPIO_OPEN_DRAIN)	/* I2C5 SDA */
ALTERNATE(G, 0x30, 1,  MODULE_UART,    0)		/* UART2 */
ALTERNATE(J, 0x40, 1,  MODULE_PECI,    0)		/* PECI Tx */
ALTERNATE(J, 0x80, 0,  MODULE_PECI,    GPIO_ANALOG)	/* PECI Rx */
ALTERNATE(L, 0x3f, 15, MODULE_LPC,     0)		/* LPC */
ALTERNATE(M, 0x33, 15, MODULE_LPC,     0)		/* LPC */
ALTERNATE(N, 0x0c, 1,  MODULE_PWM_FAN, 0)		/* FAN0PWM2 */
