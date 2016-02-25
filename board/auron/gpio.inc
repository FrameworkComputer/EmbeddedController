/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(POWER_BUTTON_L, PIN(A, 2), GPIO_INT_BOTH_DSLEEP,                power_button_interrupt)   /* Power button */
GPIO_INT(LID_OPEN,       PIN(A, 3), GPIO_INT_BOTH_DSLEEP,                lid_interrupt)            /* Lid switch */
GPIO_INT(AC_PRESENT,     PIN(H, 3), GPIO_INT_BOTH_DSLEEP,                extpower_interrupt)       /* AC power present */
GPIO_INT(PCH_BKLTEN,     PIN(M, 3), GPIO_INT_BOTH,                       backlight_interrupt)      /* Backlight enable signal from PCH */
GPIO_INT(PCH_SLP_S0_L,   PIN(G, 6), GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_S0# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,   PIN(G, 7), GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S3# signal from PCH */
GPIO_INT(PCH_SLP_S5_L,   PIN(H, 1), GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S5# signal from PCH */
GPIO_INT(PCH_SLP_SUS_L,  PIN(G, 3), GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_SUS# signal from PCH */
GPIO_INT(PP1050_PGOOD,   PIN(H, 4), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.05V */
GPIO_INT(PP1350_PGOOD,   PIN(H, 6), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.35V (DRAM) */
GPIO_INT(PP5000_PGOOD,   PIN(N, 0), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 5V */
GPIO_INT(VCORE_PGOOD,    PIN(C, 6), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on core VR */
GPIO_INT(PCH_EDP_VDD_EN, PIN(J, 1), GPIO_INT_BOTH,                       power_interrupt)          /* PCH wants EDP enabled */
GPIO_INT(RECOVERY_L,     PIN(A, 5), GPIO_PULL_UP | GPIO_INT_BOTH,        switch_interrupt)         /* Recovery signal from servo */
GPIO_INT(WP_L,           PIN(A, 4), GPIO_INT_BOTH,                       switch_interrupt)         /* Write protect input */
GPIO_INT(JTAG_TCK,       PIN(C, 0), GPIO_DEFAULT,                        jtag_interrupt)           /* JTAG clock input */
GPIO_INT(UART0_RX,       PIN(A, 0), GPIO_PULL_UP | GPIO_INT_BOTH_DSLEEP, uart_deepsleep_interrupt) /* UART0 RX input */

/* Other inputs */
GPIO(FAN_ALERT_L,          PIN(B, 0), GPIO_INPUT)    /* From thermal sensor */
GPIO(PCH_SUSWARN_L,        PIN(G, 2), GPIO_INT_BOTH) /* SUSWARN# signal from PCH */
GPIO(USB1_OC_L,            PIN(E, 7), GPIO_INPUT)    /* USB port overcurrent warning */
GPIO(USB2_OC_L,            PIN(E, 0), GPIO_INPUT)    /* USB port overcurrent warning */
GPIO(BOARD_VERSION1,       PIN(Q, 5), GPIO_INPUT)    /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2,       PIN(Q, 6), GPIO_INPUT)    /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3,       PIN(Q, 7), GPIO_INPUT)    /* Board version stuffing resistor 3 */
GPIO(CPU_PGOOD,            PIN(C, 4), GPIO_INPUT)    /* Power good to the CPU */
GPIO(BAT_PRESENT_L,        PIN(B, 4), GPIO_INPUT)    /* Battery present. Repurposed BAT_TEMP */

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,          PIN(B, 1), GPIO_OUT_LOW)  /* Force CPU to think it's overheated */
GPIO(PP1350_EN,            PIN(H, 5), GPIO_OUT_LOW)  /* Enable 1.35V supply */
GPIO(PP3300_DX_EN,         PIN(J, 2), GPIO_OUT_LOW)  /* Enable power to lots of peripherals */
GPIO(PP3300_LTE_EN,        PIN(D, 2), GPIO_OUT_LOW)  /* Enable LTE radio */
GPIO(PP3300_WLAN_EN,       PIN(J, 0), GPIO_OUT_LOW)  /* Enable WiFi power */
GPIO(SUSP_VR_EN,           PIN(C, 7), GPIO_OUT_LOW)  /* Enable 1.05V regulator */
GPIO(VCORE_EN,             PIN(C, 5), GPIO_OUT_LOW)  /* Stuffing option - not connected */
GPIO(PP5000_EN,            PIN(H, 7), GPIO_OUT_LOW)  /* Enable 5V supply */
GPIO(PP5000_FAN_EN,        PIN(J, 3), GPIO_OUT_LOW)  /* Enable fan power rail */
GPIO(SYS_PWROK,            PIN(H, 2), GPIO_OUT_LOW)  /* EC thinks everything is up and ready */
GPIO(WLAN_OFF_L,           PIN(J, 4), GPIO_OUT_LOW)  /* Disable WiFi radio */
GPIO(CHARGE_L,             PIN(E, 6), GPIO_OUT_LOW)  /* Allow battery to charge when on AC */

GPIO(ENABLE_BACKLIGHT,     PIN(M, 7), GPIO_OUT_LOW)  /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,      PIN(N, 1), GPIO_OUT_LOW)  /* Enable touchpad power */
GPIO(ENTERING_RW,          PIN(D, 3), GPIO_OUT_LOW)  /* Indicate when EC is entering RW code */
GPIO(PCH_DPWROK,           PIN(G, 0), GPIO_OUT_LOW)  /* Indicate when VccDSW is good */

/*
 * HDA_SDO is technically an output, but we need to leave it as an
 * input until we drive it high.  So can't use open-drain (HI_Z).
 */
GPIO(PCH_HDA_SDO,          PIN(G, 1), GPIO_INPUT)    /* HDA_SDO signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_WAKE_L,           PIN(F, 0), GPIO_OUT_HIGH) /* Wake signal from EC to PCH */
GPIO(PCH_NMI_L,            PIN(F, 2), GPIO_OUT_HIGH) /* Non-maskable interrupt pin to PCH */
GPIO(PCH_PWRBTN_L,         PIN(H, 0), GPIO_OUT_HIGH) /* Power button output to PCH */
GPIO(PCH_PWROK,            PIN(F, 5), GPIO_OUT_LOW)  /* PWROK / APWROK signals to PCH */

/*
 * PL6 is one of 4 pins on the EC which can't be used in open-drain
 * mode.  To work around this PCH_RCIN_L is set to an input. It will
 * only be set to an output when it needs to be driven to 0.
 */
GPIO(PCH_RCIN_L,           PIN(L, 6), GPIO_INPUT)    /* RCIN# line to PCH (for 8042 emulation) */
GPIO(PCH_RSMRST_L,         PIN(F, 1), GPIO_OUT_LOW)  /* Reset PCH resume power plane logic */
GPIO(PCH_SMI_L,            PIN(F, 4), GPIO_ODR_HIGH) /* System management interrupt to PCH */
GPIO(TOUCHSCREEN_RESET_L,  PIN(N, 7), GPIO_OUT_LOW)  /* Reset touch screen */
GPIO(EC_EDP_VDD_EN,        PIN(J, 5), GPIO_OUT_LOW)  /* Enable EDP (passthru from PCH) */

GPIO(LPC_CLKRUN_L,         PIN(M, 2), GPIO_ODR_HIGH) /* Dunno. Probably important, though. */
GPIO(USB1_ENABLE,          PIN(E, 4), GPIO_OUT_LOW)  /* USB port 1 output power enable */
GPIO(USB2_ENABLE,          PIN(D, 5), GPIO_OUT_LOW)  /* USB port 2 output power enable */

GPIO(PCH_SUSACK_L,         PIN(F, 3), GPIO_OUT_HIGH) /* Acknowledge PCH SUSWARN# signal */
GPIO(PCH_RTCRST_L,         PIN(F, 6), GPIO_ODR_HIGH) /* Not supposed to be here */
GPIO(PCH_SRTCRST_L,        PIN(F, 7), GPIO_ODR_HIGH) /* Not supposed to be here */

GPIO(BAT_LED0_L,           PIN(D, 0), GPIO_ODR_HIGH) /* Battery charging LED - blue */
GPIO(BAT_LED1_L,           PIN(N, 4), GPIO_ODR_HIGH) /* Battery charging LED - orange */
GPIO(PWR_LED0_L,           PIN(D, 1), GPIO_ODR_HIGH) /* Power LED - blue */
GPIO(PWR_LED1_L,           PIN(N, 6), GPIO_ODR_HIGH) /* Power LED - orange */

ALTERNATE(PIN_MASK(A, 0x03), 1,  MODULE_UART,    GPIO_PULL_UP)	/* UART0 */
ALTERNATE(PIN_MASK(B, 0x04), 3,  MODULE_I2C,     0)		/* I2C0 SCL */
ALTERNATE(PIN_MASK(B, 0x08), 3,  MODULE_I2C,     GPIO_OPEN_DRAIN)	/* I2C0 SDA */
ALTERNATE(PIN_MASK(B, 0x40), 3,  MODULE_I2C,     0)		/* I2C5 SCL */
ALTERNATE(PIN_MASK(B, 0x80), 3,  MODULE_I2C,     GPIO_OPEN_DRAIN)	/* I2C5 SDA */
ALTERNATE(PIN_MASK(G, 0x30), 1,  MODULE_UART,    0)		/* UART2 */
ALTERNATE(PIN_MASK(J, 0x40), 1,  MODULE_PECI,    0)		/* PECI Tx */
ALTERNATE(PIN_MASK(J, 0x80), 0,  MODULE_PECI,    GPIO_ANALOG)	/* PECI Rx */
ALTERNATE(PIN_MASK(L, 0x3f), 15, MODULE_LPC,     0)		/* LPC */
ALTERNATE(PIN_MASK(M, 0x33), 15, MODULE_LPC,     0)		/* LPC */
ALTERNATE(PIN_MASK(N, 0x0c), 1,  MODULE_PWM,     0)		/* FAN0PWM2 */
