/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO(POWER_BUTTON_L,   A, 2, GPIO_INT_BOTH_DSLEEP,                power_button_interrupt)
GPIO(LID_OPEN,         A, 3, GPIO_INT_BOTH_DSLEEP,                lid_interrupt)
GPIO(AC_PRESENT,       H, 3, GPIO_INT_BOTH_DSLEEP,                extpower_interrupt)
GPIO(PCH_SLP_S3_L,     G, 7, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, power_signal_interrupt)   /* SLP_S3# signal from PCH */
GPIO(PCH_SLP_S4_L,     H, 1, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, power_signal_interrupt)   /* SLP_S4# signal from PCH */
GPIO(PP1050_PGOOD,     H, 4, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 1.05V */
GPIO(PP3300_PCH_PGOOD, C, 4, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 3.3V (PCH supply) */
GPIO(PP5000_PGOOD,     N, 0, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on 5V */
GPIO(S5_PGOOD,         G, 0, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on S5 supplies */
GPIO(VCORE_PGOOD,      C, 6, GPIO_INT_BOTH,                       power_signal_interrupt)   /* Power good on core VR */
GPIO(WP_L,             A, 4, GPIO_INT_BOTH,                       switch_interrupt)         /* Write protect input */
GPIO(JTAG_TCK,         C, 0, GPIO_DEFAULT,                        jtag_interrupt)           /* JTAG clock input */
GPIO(UART0_RX,         A, 0, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, uart_deepsleep_interrupt) /* UART0 RX input */

/* Other inputs */
GPIO(BOARD_VERSION1, Q, 5, GPIO_INPUT, NULL)
GPIO(BOARD_VERSION2, Q, 6, GPIO_INPUT, NULL)
GPIO(BOARD_VERSION3, Q, 7, GPIO_INPUT, NULL)

#ifdef CONFIG_CHIPSET_DEBUG
GPIO(PCH_SLP_SX_L,        G, 3, GPIO_INPUT | GPIO_PULL_UP, NULL) /* SLP_S0IX# signal from PCH */
GPIO(PCH_SUS_STAT_L,      G, 6, GPIO_INPUT | GPIO_PULL_UP, NULL) /* SUS_STAT# signal from PCH */
GPIO(PCH_SUSPWRDNACK,     G, 2, GPIO_INPUT | GPIO_PULL_UP, NULL) /* SUSPWRDNACK signal from PCH */
#endif

GPIO(PP1000_S0IX_PGOOD,   H, 6, GPIO_INPUT, NULL) /* Power good on 1.00V (S0iX supplies) */
GPIO(USB1_OC_L,           E, 7, GPIO_INPUT, NULL) /* USB port overcurrent warning */
GPIO(USB2_OC_L,           E, 0, GPIO_INPUT, NULL) /* USB port overcurrent warning */

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,         B, 5, GPIO_OUT_LOW,  NULL) /* Force CPU to think it's overheated */
GPIO(ENABLE_BACKLIGHT,    M, 7, GPIO_ODR_HIGH, NULL) /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,     N, 1, GPIO_OUT_LOW,  NULL) /* Enable touchpad power */
GPIO(ENTERING_RW,         D, 6, GPIO_OUT_LOW,  NULL) /* Indicate when EC is entering RW code */
GPIO(LPC_CLKRUN_L,        M, 2, GPIO_ODR_HIGH, NULL) /* Request that PCH drive LPC clock */
GPIO(PCH_CORE_PWROK,      F, 5, GPIO_OUT_LOW,  NULL) /* Indicate core well power is stable */
GPIO(PCH_PWRBTN_L,        H, 0, GPIO_ODR_HIGH, NULL) /* Power button output to PCH */
GPIO(PCH_RCIN_L,          F, 3, GPIO_ODR_HIGH, NULL) /* Reset line to PCH (for 8042 emulation) */
GPIO(PCH_RSMRST_L,        F, 1, GPIO_OUT_LOW,  NULL) /* Reset PCH resume power plane logic */
GPIO(PCH_SMI_L,           F, 4, GPIO_ODR_HIGH, NULL) /* System management interrupt to PCH */
GPIO(PCH_SOC_OVERRIDE,    G, 1, GPIO_OUT_LOW,  NULL) /* SOC override signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_SYS_PWROK,       J, 1, GPIO_OUT_LOW,  NULL) /* EC thinks everything is up and ready */
GPIO(PCH_WAKE_L,          F, 0, GPIO_ODR_HIGH, NULL) /* Wake signal from EC to PCH */
GPIO(PP1350_EN,           H, 5, GPIO_OUT_LOW,  NULL) /* Enable 1.35V supply */
GPIO(PP3300_DX_EN,        J, 2, GPIO_OUT_LOW,  NULL) /* Enable power to lots of peripherals */
GPIO(PP3300_LTE_EN,       D, 4, GPIO_OUT_LOW,  NULL) /* Enable LTE radio */
GPIO(PP3300_WLAN_EN,      J, 0, GPIO_OUT_LOW,  NULL) /* Enable WiFi power */
GPIO(PP5000_EN,           H, 7, GPIO_OUT_LOW,  NULL) /* Enable 5V supply */
GPIO(PPSX_EN,             L, 6, GPIO_OUT_LOW,  NULL) /* Enable PP1350_PCH_SX, PP1000_PCH_SX */
GPIO(SUSP_VR_EN,          C, 7, GPIO_OUT_LOW,  NULL) /* Enable 1.05V regulator */
GPIO(TOUCHSCREEN_RESET_L, N, 7, GPIO_OUT_LOW,  NULL) /* Reset touch screen */
GPIO(USB_CTL1,            E, 6, GPIO_OUT_LOW,  NULL) /* USB control signal 1 to both ports */
GPIO(USB_ILIM_SEL,        E, 5, GPIO_OUT_LOW,  NULL) /* USB current limit to both ports */
GPIO(USB1_ENABLE,         E, 4, GPIO_OUT_LOW,  NULL) /* USB port 1 output power enable */
GPIO(USB2_ENABLE,         D, 5, GPIO_OUT_LOW,  NULL) /* USB port 2 output power enable */
GPIO(VCORE_EN,            C, 5, GPIO_OUT_LOW,  NULL) /* Enable core power supplies */
GPIO(WLAN_OFF_L,          J, 4, GPIO_OUT_LOW,  NULL) /* Disable WiFi radio */
GPIO(PCH_SCI_L,           M, 1, GPIO_ODR_HIGH, NULL) /* Assert SCI to PCH */
GPIO(KBD_IRQ_L,           M, 3, GPIO_ODR_HIGH, NULL) /* Negative edge triggered irq. */

ALTERNATE(A, 0x03, 1,  MODULE_UART,    0)		/* UART0 */
ALTERNATE(B, 0x04, 3,  MODULE_I2C,     0)		/* I2C0 SCL */
ALTERNATE(B, 0x08, 3,  MODULE_I2C,     GPIO_OPEN_DRAIN)	/* I2C0 SDA */
ALTERNATE(B, 0x40, 3,  MODULE_I2C,     0)		/* I2C5 SCL */
ALTERNATE(B, 0x80, 3,  MODULE_I2C,     GPIO_OPEN_DRAIN)	/* I2C5 SDA */
ALTERNATE(D, 0x0f, 2,  MODULE_SPI,     0)		/* SPI1 */
ALTERNATE(L, 0x3f, 15, MODULE_LPC,     0)		/* LPC */
ALTERNATE(M, 0x21, 15, MODULE_LPC,     0)		/* LPC */
ALTERNATE(N, 0x50, 1,  MODULE_PWM_LED, GPIO_OPEN_DRAIN)	/* FAN0PWM 3&4 */
