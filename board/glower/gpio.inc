/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(POWER_BUTTON_L,   PIN(203), GPIO_INT_BOTH,                power_button_interrupt)
GPIO_INT(LID_OPEN,         PIN(160), GPIO_INT_BOTH,                lid_interrupt)
GPIO_INT(AC_PRESENT,       PIN(163), GPIO_INT_BOTH,                extpower_interrupt)
GPIO_INT(PCH_SLP_S3_L,     PIN(204), GPIO_INT_BOTH | GPIO_PULL_UP, power_signal_interrupt) /* SLP_S3# signal from PCH */
GPIO_INT(PCH_SLP_S4_L,     PIN(210), GPIO_INT_BOTH | GPIO_PULL_UP, power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO_INT(PP1050_PGOOD,     PIN(133), GPIO_INT_BOTH,                power_signal_interrupt) /* Power good on 1.05V */
GPIO_INT(PP3300_PCH_PGOOD, PIN(44),  GPIO_INT_BOTH,                power_signal_interrupt) /* Power good on 3.3V (PCH supply) */
GPIO_INT(PP5000_PGOOD,     PIN(30),  GPIO_INT_BOTH,                power_signal_interrupt) /* Power good on 5V */
GPIO_INT(S5_PGOOD,         PIN(62),  GPIO_INT_BOTH,                power_signal_interrupt) /* Power good on S5 supplies */
GPIO_INT(VCORE_PGOOD,      PIN(57),  GPIO_INT_BOTH,                power_signal_interrupt) /* Power good on core VR */
GPIO_INT(WP_L,             PIN(12),  GPIO_INT_BOTH,                switch_interrupt)       /* Write protect input */

/* Other inputs */
GPIO(BOARD_VERSION1,   PIN(6),   GPIO_INPUT)
GPIO(BOARD_VERSION2,   PIN(123), GPIO_INPUT)
GPIO(BOARD_VERSION3,   PIN(127), GPIO_INPUT)

#ifdef CONFIG_CHIPSET_DEBUG
GPIO(PCH_SLP_SX_L,     PIN(211), GPIO_INPUT | GPIO_PULL_UP)
GPIO(PCH_SUS_STAT_L,   PIN(201), GPIO_INPUT | GPIO_PULL_UP)
GPIO(PCH_SUSPWRDNACK,  PIN(46),  GPIO_INPUT | GPIO_PULL_UP)
#endif

GPIO(PP1000_S0IX_PGOOD, PIN(35), GPIO_INPUT)
GPIO(USB1_OC_L,         PIN(134),GPIO_INPUT)
GPIO(USB2_OC_L,         PIN(16), GPIO_INPUT)

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,         PIN(145), GPIO_OUT_LOW)  /* Force CPU to think it's overheated */
GPIO(ENABLE_BACKLIGHT,    PIN(200), GPIO_ODR_HIGH) /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,     PIN(64),  GPIO_OUT_LOW)  /* Enable touchpad power */
GPIO(ENTERING_RW,         PIN(33),  GPIO_OUT_LOW)  /* Indicate when EC is entering RW code */
GPIO(LPC_CLKRUN_L,        PIN(14),  GPIO_ODR_HIGH) /* Request that PCH drive LPC clock */
GPIO(PCH_CORE_PWROK,      PIN(122), GPIO_OUT_LOW)  /* Indicate core well power is stable */
GPIO(PCH_PWRBTN_L,        PIN(130), GPIO_ODR_HIGH) /* Power button output to PCH */
GPIO(PCH_RCIN_L,          PIN(140), GPIO_ODR_HIGH) /* Reset line to PCH (for 8042 emulation) */
GPIO(PCH_RSMRST_L,        PIN(143), GPIO_OUT_LOW)  /* Reset PCH resume power plane logic */
GPIO(PCH_SMI_L,           PIN(55),  GPIO_ODR_HIGH) /* System management interrupt to PCH */
GPIO(PCH_SOC_OVERRIDE_L,  PIN(65),  GPIO_OUT_LOW)  /* SOC override signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_SYS_PWROK,       PIN(124), GPIO_OUT_LOW)  /* EC thinks everything is up and ready */
GPIO(PCH_WAKE_L,          PIN(202), GPIO_ODR_HIGH) /* Wake signal from EC to PCH */
GPIO(PP1350_EN,           PIN(147), GPIO_OUT_LOW)  /* Enable 1.35V supply */
GPIO(PP3300_DX_EN,        PIN(50),  GPIO_OUT_LOW)  /* Enable power to lots of peripherals */
GPIO(PP3300_LTE_EN,       PIN(11),  GPIO_OUT_LOW)  /* Enable LTE radio */
GPIO(PP3300_WLAN_EN,      PIN(47),  GPIO_OUT_LOW)  /* Enable WiFi power */
GPIO(PP5000_EN,           PIN(27),  GPIO_OUT_LOW)  /* Enable 5V supply */
GPIO(PPSX_EN,             PIN(53),  GPIO_INPUT)    /* Enable PP1350_PCH_SX, PP1000_PCH_SX */
GPIO(SUSP_VR_EN,          PIN(66),  GPIO_OUT_LOW)  /* Enable 1.05V regulator */
GPIO(TOUCHSCREEN_RESET_L, PIN(161), GPIO_OUT_HIGH) /* Reset touch screen */
GPIO(USB_CTL1,            PIN(157), GPIO_OUT_LOW)  /* USB control signal 1 to both ports */
GPIO(USB_ILIM_SEL,        PIN(36),  GPIO_OUT_LOW)  /* USB current limit to both ports */
GPIO(USB1_ENABLE,         PIN(15),  GPIO_OUT_LOW)  /* USB port 1 output power enable */
GPIO(USB2_ENABLE,         PIN(17),  GPIO_OUT_LOW)  /* USB port 2 output power enable */
GPIO(VCORE_EN,            PIN(150), GPIO_OUT_LOW)  /* Enable core power supplies */
GPIO(WLAN_OFF_L,          PIN(52),  GPIO_OUT_LOW)  /* Disable WiFi radio */
GPIO(KBD_IRQ_L,           PIN(67),  GPIO_ODR_HIGH) /* Negative edge triggered irq. */
GPIO(BAT_LED0,            PIN(154), GPIO_ODR_HIGH)
GPIO(BAT_LED1,            PIN(155), GPIO_ODR_HIGH)
GPIO(KBD_KSO2,            PIN(101), GPIO_OUT_LOW)

GPIO(SYS_RST_L,           PIN(121), GPIO_INPUT)
GPIO(EC_HIB,              PIN(34),  GPIO_INPUT)
GPIO(ICMNT,               PIN(61),  GPIO_INPUT)
GPIO(PVT_CS0,             PIN(146), GPIO_ODR_HIGH)

/* Configure VCC_PWRGD as GPIO so that it's internally gated high */
GPIO(TP78,                PIN(63),  GPIO_OUT_HIGH)


ALTERNATE(PIN_MASK(16,  0x24), 1, MODULE_UART,          0) /* UART0 */
ALTERNATE(PIN_MASK(2,   0x0f), 2, MODULE_I2C,           0) /* I2C1 and I2C2 */
ALTERNATE(PIN_MASK(0,   0xfe), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1,   0x03), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(3,   0x04), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(4,   0x0d), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(10,  0xd8), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(12,  0x60), 2, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(14,  0x14), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2,   0x40), 2, MODULE_LPC,           0) /* LPC SCI */
ALTERNATE(PIN_MASK(15,  0x08), 1, MODULE_SPI,           0)
ALTERNATE(PIN_MASK(16,  0x10), 1, MODULE_SPI,           0)
ALTERNATE(PIN_MASK(5,   0x10), 1, MODULE_SPI,           0)
ALTERNATE(PIN_MASK(1,   0x10), 1, MODULE_LPC,           0) /* 14: CLKRUN# */
ALTERNATE(PIN_MASK(11,  0x9e), 1, MODULE_LPC,           0) /* 111~114:LAD[0:3],  117:PCI_CLK */
ALTERNATE(PIN_MASK(11,  0x40), 1, MODULE_LPC,           GPIO_INT_BOTH) /* 116: LRESET# */
ALTERNATE(PIN_MASK(12,  0x01), 1, MODULE_LPC,           0) /* 120: LFRAME# */
