/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(POWER_BUTTON_L, PIN(A, 2), GPIO_INT_BOTH_DSLEEP,                power_button_interrupt)   /* Power button */
GPIO_INT(LID_OPEN,       PIN(A, 3), GPIO_INT_BOTH_DSLEEP,                lid_interrupt)            /* Lid switch */
GPIO_INT(AC_PRESENT,     PIN(H, 3), GPIO_INT_BOTH_DSLEEP,                extpower_interrupt)       /* AC power present */
GPIO_INT(PCH_SLP_S0_L,   PIN(G, 6), GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_S0# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,   PIN(G, 7), GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S3# signal from PCH */
GPIO_INT(PCH_SLP_S5_L,   PIN(H, 1), GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S5# signal from PCH */
GPIO_INT(PCH_SLP_SUS_L,  PIN(G, 3), GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_SUS# signal from PCH */
GPIO_INT(PCH_SUSWARN_L,  PIN(G, 2), GPIO_INT_BOTH,                       power_signal_interrupt)   /* SUSWARN# signal from PCH */
GPIO_INT(PP1050_PGOOD,   PIN(H, 4), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.05V */
GPIO_INT(PP1200_PGOOD,   PIN(H, 6), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.2V (DRAM) */
GPIO_INT(PP1800_PGOOD,   PIN(L, 7), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.8V (DRAM) */
GPIO_INT(VCORE_PGOOD,    PIN(C, 6), GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on core VR */
GPIO_INT(WP_L,           PIN(A, 4), GPIO_INT_BOTH,                       switch_interrupt)         /* Write protect input */
GPIO_INT(PCH_BL_EN,      PIN(M, 3), GPIO_INT_RISING,                     backlight_interrupt)      /* PCH backlight input */
GPIO_INT(JTAG_TCK,       PIN(C, 0), GPIO_DEFAULT,                        jtag_interrupt)           /* JTAG clock input */
GPIO_INT(UART0_RX,       PIN(A, 0), GPIO_PULL_UP | GPIO_INT_BOTH_DSLEEP, uart_deepsleep_interrupt) /* UART0 RX input */
GPIO_INT(BKBOOST_DET,    PIN(B, 5), GPIO_INT_RISING,                     bkboost_det_interrupt)    /* Backboost detect */

/* Interrupt signal from PD MCU, external pull-down */
GPIO_INT(PD_MCU_INT, PIN(J, 5), GPIO_INT_RISING | GPIO_INT_DSLEEP, pd_mcu_interrupt)

/*
 * Combined accelerometer input. This will become an interrupt, once we have
 * support for it.
 */
GPIO(ACCEL_INT, PIN(F, 7), GPIO_INPUT)

/*
 * Ambient Light Sensor input.  This could become an interrupt once supported.
 */
GPIO(ALS_INT_L, PIN(N, 0), GPIO_INPUT)

/* Other inputs */
GPIO(BOARD_VERSION1, PIN(Q, 7), GPIO_INPUT) /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2, PIN(Q, 6), GPIO_INPUT) /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3, PIN(Q, 5), GPIO_INPUT) /* Board version stuffing resistor 3 */
GPIO(USB1_OC_L,      PIN(E, 7), GPIO_INPUT) /* USB port overcurrent warning */
GPIO(USB1_STATUS_L,  PIN(E, 6), GPIO_INPUT) /* USB charger port 1 status output */
GPIO(USB2_OC_L,      PIN(E, 0), GPIO_INPUT) /* USB port overcurrent warning */
GPIO(USB2_STATUS_L,  PIN(D, 7), GPIO_INPUT) /* USB charger port 2 status output */
GPIO(PD_IN_RW,       PIN(A, 5), GPIO_INPUT) /* PD is in RW */
GPIO(PCH_HDA_SDO_L,  PIN(G, 1), GPIO_INPUT) /* HDA_SDO signal to PCH to disable ME */

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,         PIN(B, 1), GPIO_OUT_LOW)  /* Force CPU to think it's overheated */
GPIO(PP1200_EN,           PIN(H, 5), GPIO_OUT_LOW)  /* Enable 1.20V supply */
GPIO(PP3300_DSW_EN,       PIN(F, 6), GPIO_OUT_LOW)  /* Enable 3.3V DSW rail */
GPIO(PP3300_DSW_GATED_EN, PIN(J, 3), GPIO_OUT_LOW)  /* Enable 3.3V Gated DSW and core VDD */
GPIO(PP3300_LTE_EN,       PIN(D, 2), GPIO_OUT_LOW)  /* Enable LTE radio */
GPIO(PP3300_WLAN_EN,      PIN(J, 0), GPIO_OUT_LOW)  /* Enable WiFi power */
GPIO(PP1050_EN,           PIN(C, 7), GPIO_OUT_LOW)  /* Enable 1.05V regulator */
GPIO(PP5000_USB_EN,       PIN(C, 5), GPIO_OUT_LOW)  /* Enable USB power */
GPIO(PP5000_EN,           PIN(H, 7), GPIO_OUT_LOW)  /* Enable 5V supply */
GPIO(PP1800_EN,           PIN(L, 6), GPIO_OUT_LOW)  /* Enable 1.8V supply */
GPIO(SYS_PWROK,           PIN(H, 2), GPIO_OUT_LOW)  /* EC thinks everything is up and ready */
GPIO(WLAN_OFF_L,          PIN(J, 4), GPIO_OUT_LOW)  /* Disable WiFi radio */
GPIO(USB_MCU_RST,         PIN(B, 0), GPIO_OUT_LOW)  /* USB PD MCU reset */
GPIO(ENABLE_BACKLIGHT,    PIN(M, 7), GPIO_OUT_LOW)  /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,     PIN(N, 1), GPIO_OUT_LOW)  /* Enable touchpad power */
GPIO(ENTERING_RW,         PIN(D, 3), GPIO_OUT_LOW)  /* Indicate when EC is entering RW code */
GPIO(LIGHTBAR_RESET_L,    PIN(J, 2), GPIO_ODR_LOW)  /* Reset lightbar controllers */
GPIO(PCH_DPWROK,          PIN(G, 0), GPIO_OUT_LOW)  /* Indicate when VccDSW is good */
GPIO(PCH_RSMRST_L,        PIN(C, 4), GPIO_OUT_LOW)  /* Reset PCH resume power plane logic */
GPIO(PCH_RTCRST_L,        PIN(J, 1), GPIO_ODR_HIGH) /* Reset PCH RTC well */
GPIO(PCH_WAKE_L,          PIN(F, 0), GPIO_ODR_HIGH) /* Wake signal from EC to PCH */
GPIO(PCH_NMI_L,           PIN(F, 2), GPIO_ODR_HIGH) /* Non-maskable interrupt pin to PCH */
GPIO(PCH_PWRBTN_L,        PIN(H, 0), GPIO_ODR_HIGH) /* Power button output to PCH */
GPIO(PCH_PWROK,           PIN(F, 5), GPIO_OUT_LOW)  /* PWROK / APWROK signals to PCH */
GPIO(PCH_RCIN_L,          PIN(F, 3), GPIO_ODR_HIGH) /* RCIN# line to PCH (for 8042 emulation) */
GPIO(PCH_SYS_RST_L,       PIN(F, 1), GPIO_ODR_HIGH) /* Reset PCH resume power plane logic */
GPIO(PCH_SMI_L,           PIN(F, 4), GPIO_ODR_HIGH) /* System management interrupt to PCH */
GPIO(TOUCHSCREEN_RESET_L, PIN(N, 7), GPIO_ODR_LOW)  /* Reset touch screen */
GPIO(PCH_ACOK,            PIN(M, 6), GPIO_OUT_LOW)  /* AC present signal buffered to PCH */
#ifndef HEY_USE_BUILTIN_CLKRUN
GPIO(LPC_CLKRUN_L,        PIN(M, 2), GPIO_ODR_HIGH) /* Dunno. Probably important, though. */
#endif
GPIO(USB1_CTL1,           PIN(E, 1), GPIO_OUT_LOW)  /* USB charger port 1 CTL1 output */
GPIO(USB1_CTL2,           PIN(E, 2), GPIO_OUT_HIGH) /* USB charger port 1 CTL2 output */
GPIO(USB1_CTL3,           PIN(E, 3), GPIO_OUT_LOW)  /* USB charger port 1 CTL3 output */
GPIO(USB1_ENABLE,         PIN(E, 4), GPIO_OUT_HIGH) /* USB charger port 1 enable */
GPIO(USB1_ILIM_SEL_L,     PIN(E, 5), GPIO_OUT_HIGH) /* USB charger port 1 ILIM_SEL output */
GPIO(USB2_CTL1,           PIN(D, 0), GPIO_OUT_LOW)  /* USB charger port 2 CTL1 output */
GPIO(USB2_CTL2,           PIN(D, 1), GPIO_OUT_HIGH) /* USB charger port 2 CTL2 output */
GPIO(USB2_CTL3,           PIN(D, 4), GPIO_OUT_LOW)  /* USB charger port 2 CTL3 output */
GPIO(USB2_ENABLE,         PIN(D, 5), GPIO_OUT_HIGH) /* USB charger port 2 enable */
GPIO(USB2_ILIM_SEL_L,     PIN(D, 6), GPIO_OUT_HIGH) /* USB charger port 2 ILIM_SEL output */

GPIO(I2C0_SCL,            PIN(B, 2), GPIO_ODR_HIGH) /* I2C port 0 SCL */
GPIO(I2C0_SDA,            PIN(B, 3), GPIO_ODR_HIGH) /* I2C port 0 SDA */
GPIO(I2C1_SCL,            PIN(A, 6), GPIO_ODR_HIGH) /* I2C port 1 SCL */
GPIO(I2C1_SDA,            PIN(A, 7), GPIO_ODR_HIGH) /* I2C port 1 SDA */
GPIO(I2C5_SCL,            PIN(B, 6), GPIO_ODR_HIGH) /* I2C port 5 SCL */
GPIO(I2C5_SDA,            PIN(B, 7), GPIO_ODR_HIGH) /* I2C port 5 SDA */

ALTERNATE(PIN_MASK(A, 0x03), 1,  MODULE_UART, 0)               /* UART0 */
ALTERNATE(PIN_MASK(A, 0x40), 3,  MODULE_I2C,  0)               /* I2C1 SCL */
ALTERNATE(PIN_MASK(A, 0x80), 3,  MODULE_I2C,  GPIO_OPEN_DRAIN) /* I2C1 SDA */
ALTERNATE(PIN_MASK(B, 0x04), 3,  MODULE_I2C,  0)               /* I2C0 SCL */
ALTERNATE(PIN_MASK(B, 0x08), 3,  MODULE_I2C,  GPIO_OPEN_DRAIN) /* I2C0 SDA */
ALTERNATE(PIN_MASK(B, 0x40), 3,  MODULE_I2C,  0)               /* I2C5 SCL */
ALTERNATE(PIN_MASK(B, 0x80), 3,  MODULE_I2C,  GPIO_OPEN_DRAIN) /* I2C5 SDA */
ALTERNATE(PIN_MASK(G, 0x30), 1,  MODULE_UART, 0)               /* UART2 */
ALTERNATE(PIN_MASK(J, 0x40), 1,  MODULE_PECI, 0)               /* PECI Tx */
ALTERNATE(PIN_MASK(J, 0x80), 0,  MODULE_PECI, GPIO_ANALOG)     /* PECI Rx */
ALTERNATE(PIN_MASK(L, 0x3f), 15, MODULE_LPC,  0)               /* LPC */
ALTERNATE(PIN_MASK(M, 0x33), 15, MODULE_LPC,  0)               /* LPC */
#ifdef HEY_USE_BUILTIN_CLKRUN
ALTERNATE(PIN_MASK(M, 0x04), 15, MODULE_LPC,  GPIO_OPEN_DRAIN) /* LPC */
#endif

ALTERNATE(PIN_MASK(N, 0x3c), 1,  MODULE_PWM,  0) /* FAN0PWM 2&3 */
ALTERNATE(PIN_MASK(N, 0x40), 1,  MODULE_PWM,  0) /* FAN0PWM4 */
