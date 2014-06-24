/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
/* Power button */
GPIO(POWER_BUTTON_L,       A, 2, GPIO_INT_BOTH_DSLEEP, power_button_interrupt)
/* Lid switch */
GPIO(LID_OPEN,             A, 3, GPIO_INT_BOTH_DSLEEP, lid_interrupt)
/* AC power present */
GPIO(AC_PRESENT,           H, 3, GPIO_INT_BOTH_DSLEEP, extpower_interrupt)
/* SLP_S3# signal from PCH */
GPIO(PCH_SLP_S3_L,         G, 7, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, power_signal_interrupt)
/* SLP_S4# signal from PCH */
GPIO(PCH_SLP_S4_L,         H, 1, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, power_signal_interrupt)
/* Power good on 1.05V */
GPIO(PP1050_PGOOD,         H, 4, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on 3.3V (PCH supply) */
GPIO(PP3300_PCH_PGOOD,     C, 4, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on 5V */
GPIO(PP5000_PGOOD,         N, 0, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on S5 supplies */
GPIO(S5_PGOOD,             G, 0, GPIO_INT_BOTH, power_signal_interrupt)
/* Power good on core VR */
GPIO(VCORE_PGOOD,          C, 6, GPIO_INT_BOTH, power_signal_interrupt)
/* Write protect input */
GPIO(WP_L,                 A, 4, GPIO_INT_BOTH, switch_interrupt)
/* JTAG clock input */
GPIO(JTAG_TCK,             C, 0, GPIO_DEFAULT, jtag_interrupt)
/* UART0 RX input */
GPIO(UART0_RX,             A, 0, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, uart_deepsleep_interrupt)

/* Other inputs */
/* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION1,       Q, 5, GPIO_INPUT, NULL)
/* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION2,       Q, 6, GPIO_INPUT, NULL)
/* Board version stuffing resistor 3 */
GPIO(BOARD_VERSION3,       Q, 7, GPIO_INPUT, NULL)
#ifdef CONFIG_CHIPSET_DEBUG
/* SLP_S0IX# signal from PCH */
GPIO(PCH_SLP_SX_L,         G, 3, GPIO_INPUT | GPIO_PULL_UP, NULL)
/* SUS_STAT# signal from PCH */
GPIO(PCH_SUS_STAT_L,       G, 6, GPIO_INPUT | GPIO_PULL_UP, NULL)
/* SUSPWRDNACK signal from PCH */
GPIO(PCH_SUSPWRDNACK,      G, 2, GPIO_INPUT | GPIO_PULL_UP, NULL)
#endif
/* Power good on 1.00V (S0iX supplies) */
GPIO(PP1000_S0IX_PGOOD,    H, 6, GPIO_INPUT, NULL)
/* USB port overcurrent warning */
GPIO(USB1_OC_L,            E, 7, GPIO_INPUT, NULL)
/* USB port overcurrent warning */
GPIO(USB2_OC_L,            E, 0, GPIO_INPUT, NULL)

/* Outputs; all unasserted by default except for reset signals */
/* Force CPU to think it's overheated */
GPIO(CPU_PROCHOT,          B, 5, GPIO_OUT_LOW,  NULL)
/* Enable backlight power */
GPIO(ENABLE_BACKLIGHT,     M, 7, GPIO_ODR_HIGH, NULL)
/* Enable touchpad power */
GPIO(ENABLE_TOUCHPAD,      N, 1, GPIO_OUT_LOW,  NULL)
/* Indicate when EC is entering RW code */
GPIO(ENTERING_RW,          D, 6, GPIO_OUT_LOW,  NULL)
/* Request that PCH drive LPC clock */
GPIO(LPC_CLKRUN_L,         M, 2, GPIO_ODR_HIGH, NULL)
/* Indicate core well power is stable */
GPIO(PCH_CORE_PWROK,       F, 5, GPIO_OUT_LOW,  NULL)
/* Power button output to PCH */
GPIO(PCH_PWRBTN_L,         H, 0, GPIO_ODR_HIGH, NULL)
/* Reset line to PCH (for 8042 emulation) */
GPIO(PCH_RCIN_L,           F, 3, GPIO_ODR_HIGH, NULL)
/* Reset PCH resume power plane logic */
GPIO(PCH_RSMRST_L,         F, 1, GPIO_OUT_LOW,  NULL)
/* System management interrupt to PCH */
GPIO(PCH_SMI_L,            F, 4, GPIO_ODR_HIGH, NULL)
/* SOC override signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_SOC_OVERRIDE,     G, 1, GPIO_OUT_LOW,  NULL)
/* EC thinks everything is up and ready */
GPIO(PCH_SYS_PWROK,        J, 1, GPIO_OUT_LOW,  NULL)
/* Wake signal from EC to PCH */
GPIO(PCH_WAKE_L,           F, 0, GPIO_ODR_HIGH, NULL)
/* Enable 1.35V supply */
GPIO(PP1350_EN,            H, 5, GPIO_OUT_LOW,  NULL)
/* Enable power to lots of peripherals */
GPIO(PP3300_DX_EN,         J, 2, GPIO_OUT_LOW,  NULL)
/* Enable LTE radio */
GPIO(PP3300_LTE_EN,        D, 4, GPIO_OUT_LOW,  NULL)
/* Enable WiFi power */
GPIO(PP3300_WLAN_EN,       J, 0, GPIO_OUT_LOW,  NULL)
/* Enable 5V supply */
GPIO(PP5000_EN,            H, 7, GPIO_OUT_LOW,  NULL)
/* Enable PP1350_PCH_SX, PP1000_PCH_SX */
GPIO(PPSX_EN,              L, 6, GPIO_OUT_LOW,  NULL)
/* Enable 1.05V regulator */
GPIO(SUSP_VR_EN,           C, 7, GPIO_OUT_LOW,  NULL)
/* Reset touch screen */
GPIO(TOUCHSCREEN_RESET_L,  N, 7, GPIO_OUT_LOW,  NULL)
/* USB control signal 1 to both ports */
GPIO(USB_CTL1,             E, 6, GPIO_OUT_LOW,  NULL)
/* USB current limit to both ports */
GPIO(USB_ILIM_SEL,         E, 5, GPIO_OUT_LOW,  NULL)
/* USB port 1 output power enable */
GPIO(USB1_ENABLE,          E, 4, GPIO_OUT_LOW,  NULL)
/* USB port 2 output power enable */
GPIO(USB2_ENABLE,          D, 5, GPIO_OUT_LOW,  NULL)
/* Enable core power supplies */
GPIO(VCORE_EN,             C, 5, GPIO_OUT_LOW,  NULL)
/* Disable WiFi radio */
GPIO(WLAN_OFF_L,           J, 4, GPIO_OUT_LOW,  NULL)
/* Assert SCI to PCH */
GPIO(PCH_SCI_L,            M, 1, GPIO_ODR_HIGH, NULL)
/* Negative edge triggered irq. */
GPIO(KBD_IRQ_L,            M, 3, GPIO_ODR_HIGH, NULL)
