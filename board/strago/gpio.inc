/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(POWER_BUTTON_L,    PORT(3),  5, GPIO_INT_BOTH_DSLEEP,                      power_button_interrupt) /* Power button */
GPIO(LID_OPEN,          PORT(2),  7, GPIO_INT_BOTH_DSLEEP,                      lid_interrupt)          /* Lid switch */
GPIO(PCH_PWRBTN_L,      PORT(16), 0, GPIO_OUT_HIGH,                             NULL)                   /* Power button output to PCH */
GPIO(STARTUP_LATCH_SET, PORT(4),  6, GPIO_OUT_HIGH,                             NULL)                   /* To enable power button detection */

GPIO(AC_PRESENT,        PORT(3),  0, GPIO_INT_BOTH_DSLEEP,                      extpower_interrupt)     /* BC_ACOK / EC_ACIN - to know if battery or AC connected */
GPIO(RSMRST_L_PGOOD,    PORT(6),  3, GPIO_INT_BOTH,                             power_signal_interrupt) /* RSMRST_N_PWRGD from power logic */
GPIO(PCH_RSMRST_L,      PORT(14), 3, GPIO_OUT_LOW,                              NULL)                   /* RSMRST_N to PCH */
GPIO(PCH_SLP_S4_L,      PORT(20), 0, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO(PCH_SLP_S3_L,      PORT(20), 6, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S3# signal from PCH */
GPIO(ALL_SYS_PGOOD,     PORT(13), 0, GPIO_INT_BOTH_DSLEEP,                      power_signal_interrupt) /* ALL_SYS_PWRGD from power logic */
GPIO(PCH_SYS_PWROK,     PORT(6),  5, GPIO_OUT_LOW,                              NULL)                   /* EC thinks everything is up and ready (DELAY_ALL_SYS_PWRGD) */

GPIO(USB3_PWR_EN,       PORT(5),  7, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for USB3 Port */
GPIO(USB2_PWR_EN,       PORT(3),  6, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for USB2 Port */
GPIO(USB_CTL1,          PORT(10), 5, GPIO_OUT_HIGH,                             NULL)                   /* USB charging mode control */
GPIO(USB_ILIM_SEL,      PORT(1),  3, GPIO_OUT_HIGH,                             NULL)                   /* USB current control */

GPIO(KBD_IRQ_L,         PORT(15), 2, GPIO_ODR_HIGH,                             NULL)                   /* Negative edge triggered irq. */

UNIMPLEMENTED(CPU_PROCHOT)
UNIMPLEMENTED(PCH_RCIN_L)

GPIO(PCH_SMI_L,         PORT(4),  4, GPIO_ODR_HIGH,                             NULL)                   /* SMI output */
GPIO(PCH_WAKE_L,        PORT(6),  6, GPIO_ODR_HIGH,                             NULL)                   /* PCH wake pin */
GPIO(KBD_KSO2,          PORT(10), 1, GPIO_KB_OUTPUT_COL2,                       NULL)                   /* Negative edge triggered irq. */

/*
 * Signals which aren't implemented on MEC1322 eval board but we'll
 * emulate anyway, to make it more convenient to debug other code.
 */
UNIMPLEMENTED(ENTERING_RW) /* EC entering RW code */

/* Alternate functions GPIO definition */
ALTERNATE(PORT(16), 0x24,     1, MODULE_UART,          0) 			/* UART0 */
ALTERNATE(PORT(0),  0x3f,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PORT(10), 0xdd,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PORT(3),  0x04,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(4),  0x0d,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(12), 0x60,     2, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(14), 0x14,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(1),  0x10,     1, MODULE_LPC,           0)                       /* 14: CLKRUN# */
ALTERNATE(PORT(11), 0x9e,     1, MODULE_LPC,           0)                       /* 111~114:LAD[0:3],  117:PCI_CLK */
ALTERNATE(PORT(11), 0x40,     1, MODULE_LPC,           GPIO_INT_BOTH)           /* 116: LRESET# */
ALTERNATE(PORT(12), 0x01,     1, MODULE_LPC,           0)                       /* 120: LFRAME# */
ALTERNATE(PORT(5),  0x10,     1, MODULE_SPI,           0)
