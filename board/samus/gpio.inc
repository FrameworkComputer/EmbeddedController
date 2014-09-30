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
GPIO(PCH_SLP_S0_L,   G, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_S0# signal from PCH */
GPIO(PCH_SLP_S3_L,   G, 7, GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S3# signal from PCH */
GPIO(PCH_SLP_S5_L,   H, 1, GPIO_INT_BOTH_DSLEEP,                power_signal_interrupt)   /* SLP_S5# signal from PCH */
GPIO(PCH_SLP_SUS_L,  G, 3, GPIO_INT_BOTH,                       power_signal_interrupt)   /* SLP_SUS# signal from PCH */
GPIO(PCH_SUSWARN_L,  G, 2, GPIO_INT_BOTH,                       power_signal_interrupt)   /* SUSWARN# signal from PCH */
GPIO(PP1050_PGOOD,   H, 4, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.05V */
GPIO(PP1200_PGOOD,   H, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.2V (DRAM) */
GPIO(PP1800_PGOOD,   L, 7, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.8V (DRAM) */
GPIO(VCORE_PGOOD,    C, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on core VR */
GPIO(WP_L,           A, 4, GPIO_INT_BOTH,                       switch_interrupt)         /* Write protect input */
GPIO(PCH_BL_EN,      M, 3, GPIO_INT_RISING,                     backlight_interrupt)      /* PCH backlight input */
GPIO(JTAG_TCK,       C, 0, GPIO_DEFAULT,                        jtag_interrupt)           /* JTAG clock input */
GPIO(UART0_RX,       A, 0, GPIO_PULL_UP | GPIO_INT_BOTH_DSLEEP, uart_deepsleep_interrupt) /* UART0 RX input */

/*
 * Combined accelerometer input. This will become an interrupt, once we have
 * support for it.
 */
GPIO(ACCEL_INT, F, 7, GPIO_INPUT, NULL)

/*
 * Ambient Light Sensor input.  This could become an interrupt once supported.
 */
GPIO(ALS_INT_L, N, 0, GPIO_INPUT, NULL)

/* Interrupt signal from PD MCU, external pull-down */
GPIO(PD_MCU_INT, J, 5, GPIO_INT_RISING | GPIO_INT_DSLEEP, pd_mcu_interrupt)

/* Other inputs */
GPIO(BOARD_VERSION1, Q, 7, GPIO_INPUT, NULL) /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2, Q, 6, GPIO_INPUT, NULL) /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3, Q, 5, GPIO_INPUT, NULL) /* Board version stuffing resistor 3 */
GPIO(USB1_OC_L,      E, 7, GPIO_INPUT, NULL) /* USB port overcurrent warning */
GPIO(USB1_STATUS_L,  E, 6, GPIO_INPUT, NULL) /* USB charger port 1 status output */
GPIO(USB2_OC_L,      E, 0, GPIO_INPUT, NULL) /* USB port overcurrent warning */
GPIO(USB2_STATUS_L,  D, 7, GPIO_INPUT, NULL) /* USB charger port 2 status output */
GPIO(PD_IN_RW,       A, 5, GPIO_INPUT, NULL) /* PD is in RW */
GPIO(PCH_HDA_SDO_L,  G, 1, GPIO_INPUT, NULL) /* HDA_SDO signal to PCH to disable ME */

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,         B, 1, GPIO_OUT_LOW,  NULL) /* Force CPU to think it's overheated */
GPIO(PP1200_EN,           H, 5, GPIO_OUT_LOW,  NULL) /* Enable 1.20V supply */
GPIO(PP3300_DSW_EN,       F, 6, GPIO_OUT_LOW,  NULL) /* Enable 3.3V DSW rail */
GPIO(PP3300_DSW_GATED_EN, J, 3, GPIO_OUT_LOW,  NULL) /* Enable 3.3V Gated DSW and core VDD */
GPIO(PP3300_LTE_EN,       D, 2, GPIO_OUT_LOW,  NULL) /* Enable LTE radio */
GPIO(PP3300_WLAN_EN,      J, 0, GPIO_OUT_LOW,  NULL) /* Enable WiFi power */
GPIO(PP1050_EN,           C, 7, GPIO_OUT_LOW,  NULL) /* Enable 1.05V regulator */
GPIO(PP5000_USB_EN,       C, 5, GPIO_OUT_LOW,  NULL) /* Enable USB power */
GPIO(PP5000_EN,           H, 7, GPIO_OUT_LOW,  NULL) /* Enable 5V supply */
GPIO(PP1800_EN,           L, 6, GPIO_OUT_LOW,  NULL) /* Enable 1.8V supply */
GPIO(SYS_PWROK,           H, 2, GPIO_OUT_LOW,  NULL) /* EC thinks everything is up and ready */
GPIO(WLAN_OFF_L,          J, 4, GPIO_OUT_LOW,  NULL) /* Disable WiFi radio */
GPIO(USB_MCU_RST,         B, 0, GPIO_OUT_LOW,  NULL) /* USB PD MCU reset */
GPIO(ENABLE_BACKLIGHT,    M, 7, GPIO_OUT_LOW,  NULL) /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,     N, 1, GPIO_OUT_LOW,  NULL) /* Enable touchpad power */
GPIO(ENTERING_RW,         D, 3, GPIO_OUT_LOW,  NULL) /* Indicate when EC is entering RW code */
GPIO(LIGHTBAR_RESET_L,    J, 2, GPIO_ODR_LOW,  NULL) /* Reset lightbar controllers */
GPIO(PCH_DPWROK,          G, 0, GPIO_OUT_LOW,  NULL) /* Indicate when VccDSW is good */
GPIO(PCH_RSMRST_L,        C, 4, GPIO_OUT_LOW,  NULL) /* Reset PCH resume power plane logic */
GPIO(PCH_RTCRST_L,        J, 1, GPIO_ODR_HIGH, NULL) /* Reset PCH RTC well */
GPIO(PCH_WAKE_L,          F, 0, GPIO_ODR_HIGH, NULL) /* Wake signal from EC to PCH */
GPIO(PCH_NMI_L,           F, 2, GPIO_ODR_HIGH, NULL) /* Non-maskable interrupt pin to PCH */
GPIO(PCH_PWRBTN_L,        H, 0, GPIO_ODR_HIGH, NULL) /* Power button output to PCH */
GPIO(PCH_PWROK,           F, 5, GPIO_OUT_LOW,  NULL) /* PWROK / APWROK signals to PCH */
GPIO(PCH_RCIN_L,          F, 3, GPIO_ODR_HIGH, NULL) /* RCIN# line to PCH (for 8042 emulation) */
GPIO(PCH_SYS_RST_L,       F, 1, GPIO_ODR_HIGH, NULL) /* Reset PCH resume power plane logic */
GPIO(PCH_SMI_L,           F, 4, GPIO_ODR_HIGH, NULL) /* System management interrupt to PCH */
GPIO(TOUCHSCREEN_RESET_L, N, 7, GPIO_ODR_LOW,  NULL) /* Reset touch screen */
GPIO(PCH_ACOK,            M, 6, GPIO_OUT_LOW,  NULL) /* AC present signal buffered to PCH */
#ifndef HEY_USE_BUILTIN_CLKRUN
GPIO(LPC_CLKRUN_L,        M, 2, GPIO_ODR_HIGH, NULL) /* Dunno. Probably important, though. */
#endif
GPIO(USB1_CTL1,           E, 1, GPIO_OUT_LOW,  NULL) /* USB charger port 1 CTL1 output */
GPIO(USB1_CTL2,           E, 2, GPIO_OUT_HIGH, NULL) /* USB charger port 1 CTL2 output */
GPIO(USB1_CTL3,           E, 3, GPIO_OUT_LOW,  NULL) /* USB charger port 1 CTL3 output */
GPIO(USB1_ENABLE,         E, 4, GPIO_OUT_HIGH, NULL) /* USB charger port 1 enable */
GPIO(USB1_ILIM_SEL_L,     E, 5, GPIO_OUT_HIGH, NULL) /* USB charger port 1 ILIM_SEL output */
GPIO(USB2_CTL1,           D, 0, GPIO_OUT_LOW,  NULL) /* USB charger port 2 CTL1 output */
GPIO(USB2_CTL2,           D, 1, GPIO_OUT_HIGH, NULL) /* USB charger port 2 CTL2 output */
GPIO(USB2_CTL3,           D, 4, GPIO_OUT_LOW,  NULL) /* USB charger port 2 CTL3 output */
GPIO(USB2_ENABLE,         D, 5, GPIO_OUT_HIGH, NULL) /* USB charger port 2 enable */
GPIO(USB2_ILIM_SEL_L,     D, 6, GPIO_OUT_HIGH, NULL) /* USB charger port 2 ILIM_SEL output */

GPIO(I2C0_SCL,            B, 2, GPIO_ODR_HIGH, NULL) /* I2C port 0 SCL */
GPIO(I2C0_SDA,            B, 3, GPIO_ODR_HIGH, NULL) /* I2C port 0 SDA */
GPIO(I2C1_SCL,            A, 6, GPIO_ODR_HIGH, NULL) /* I2C port 1 SCL */
GPIO(I2C1_SDA,            A, 7, GPIO_ODR_HIGH, NULL) /* I2C port 1 SDA */
GPIO(I2C5_SCL,            B, 6, GPIO_ODR_HIGH, NULL) /* I2C port 5 SCL */
GPIO(I2C5_SDA,            B, 7, GPIO_ODR_HIGH, NULL) /* I2C port 5 SDA */

ALTERNATE(A, 0x03, 1,  MODULE_UART, 0)               /* UART0 */
ALTERNATE(A, 0x40, 3,  MODULE_I2C,  0)               /* I2C1 SCL */
ALTERNATE(A, 0x80, 3,  MODULE_I2C,  GPIO_OPEN_DRAIN) /* I2C1 SDA */
ALTERNATE(B, 0x04, 3,  MODULE_I2C,  0)               /* I2C0 SCL */
ALTERNATE(B, 0x08, 3,  MODULE_I2C,  GPIO_OPEN_DRAIN) /* I2C0 SDA */
ALTERNATE(B, 0x40, 3,  MODULE_I2C,  0)               /* I2C5 SCL */
ALTERNATE(B, 0x80, 3,  MODULE_I2C,  GPIO_OPEN_DRAIN) /* I2C5 SDA */
ALTERNATE(G, 0x30, 1,  MODULE_UART, 0)               /* UART2 */
ALTERNATE(J, 0x40, 1,  MODULE_PECI, 0)               /* PECI Tx */
ALTERNATE(J, 0x80, 0,  MODULE_PECI, GPIO_ANALOG)     /* PECI Rx */
ALTERNATE(L, 0x3f, 15, MODULE_LPC,  0)               /* LPC */
ALTERNATE(M, 0x33, 15, MODULE_LPC,  0)               /* LPC */
#ifdef HEY_USE_BUILTIN_CLKRUN
ALTERNATE(M, 0x04, 15, MODULE_LPC,  GPIO_OPEN_DRAIN) /* LPC */
#endif

ALTERNATE(N, 0x3c, 1,  MODULE_PWM_FAN,     0) /* FAN0PWM 2&3 */
ALTERNATE(N, 0x40, 1,  MODULE_PWM_KBLIGHT, 0) /* FAN0PWM4 */
