/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(LID_OPEN,          PIN(27),  GPIO_INT_BOTH_DSLEEP,                      lid_interrupt)          /* Lid switch */
GPIO_INT(AC_PRESENT,        PIN(30),  GPIO_INT_BOTH_DSLEEP,                      extpower_interrupt)     /* BC_ACOK / EC_ACIN - to know if battery or AC connected */
GPIO_INT(WP_L,              PIN(33),  GPIO_INT_BOTH,                             switch_interrupt)       /* Write protect input */
GPIO_INT(POWER_BUTTON_L,    PIN(35),  GPIO_INT_BOTH_DSLEEP,                      power_button_interrupt) /* Power button */
GPIO_INT(RSMRST_L_PGOOD,    PIN(63),  GPIO_INT_BOTH,                             power_signal_interrupt) /* RSMRST_N_PWRGD from power logic */
GPIO_INT(ALL_SYS_PGOOD,     PIN(130), GPIO_INT_BOTH_DSLEEP,                      power_signal_interrupt) /* ALL_SYS_PWRGD from power logic */
GPIO_INT(PCH_SLP_S4_L,      PIN(200), GPIO_INT_BOTH_DSLEEP,                      power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,      PIN(206), GPIO_INT_BOTH_DSLEEP,                      power_signal_interrupt) /* SLP_S3# signal from PCH */

GPIO(NC_012,            PIN(12), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(USB_ILIM_SEL,      PIN(13), GPIO_OUT_HIGH)                   /* USB current control */
GPIO(I2C0_0_SCL,        PIN(15), GPIO_ODR_HIGH)
GPIO(I2C0_0_SDA,        PIN(16), GPIO_ODR_HIGH)
GPIO(BOARD_VERSION3,    PIN(17), GPIO_INPUT)                      /* Board ID */

GPIO(I2C2_SCL,          PIN(20), GPIO_ODR_HIGH)
GPIO(I2C2_SDA,          PIN(21), GPIO_ODR_HIGH)
GPIO(I2C1_SCL,          PIN(22), GPIO_ODR_HIGH)
GPIO(I2C1_SDA,          PIN(23), GPIO_ODR_HIGH)
GPIO(I2C3_SCL,          PIN(24), GPIO_ODR_HIGH)
GPIO(I2C3_SDA,          PIN(25), GPIO_ODR_HIGH)
GPIO(PCH_SCI_L,         PIN(26), GPIO_ODR_HIGH)                   /* SCI output */

GPIO(NC_31,             PIN(31), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(NC_34,             PIN(34), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(USB2_ENABLE,       PIN(36), GPIO_OUT_LOW)                    /* Enable power for USB2 Port */

GPIO(ENTERING_RW,       PIN(41), GPIO_OUT_LOW)                    /* Indicate when EC is entering RW code */
GPIO(PCH_SMI_L,         PIN(44), GPIO_ODR_HIGH)                   /* SMI output */
GPIO(USB_OC1_L,         PIN(45), GPIO_INT_FALLING)                /* DB2 BC1.2 over current signal to EC */
GPIO(NC_46,             PIN(46), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(NC_47,             PIN(47), GPIO_INPUT | GPIO_PULL_UP)       /* NC */

GPIO(TOUCHPANEL_PWREN,  PIN(50), GPIO_OUT_HIGH)                   /* Enable power for Touch Panel */
GPIO(PCH_SUS_STAT_L,    PIN(51), GPIO_INT_FALLING)                /* Signal to inform EC that SOC is entering low power state */
GPIO(TP_INT_DISABLE,    PIN(52), GPIO_OUT_LOW)                    /* Disable Track Pad interrupt */
GPIO(TRACKPAD_PWREN,    PIN(53), GPIO_OUT_HIGH)                   /* Enable power for Track Pad */
GPIO(USB_OC0_L,         PIN(55), GPIO_INT_FALLING)                /* Over current signal of the BC1.2 charger to EC */
GPIO(NC_56,             PIN(56), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(NC_57,             PIN(57), GPIO_INPUT | GPIO_PULL_UP)       /* NC */

GPIO(CHGR_PMON,         PIN(60), GPIO_ANALOG)
GPIO(WIFI_PWREN,        PIN(61), GPIO_OUT_HIGH)                   /* Enable power for WiFi */
GPIO(BATT_EN_L,         PIN(62), GPIO_INPUT)                      /* Will be NC */
GPIO(EC_HIB_L,          PIN(64), GPIO_OUT_LOW)
GPIO(PCH_SYS_PWROK,     PIN(65), GPIO_OUT_LOW)                    /* EC thinks everything is up and ready (DELAY_ALL_SYS_PWRGD) */
GPIO(PCH_WAKE_L,        PIN(66), GPIO_ODR_HIGH)                   /* PCH wake pin */
GPIO(USB1_ENABLE,       PIN(67), GPIO_OUT_LOW)                    /* Enable power for USB3 Port */

GPIO(USB_CTL1,          PIN(105), GPIO_OUT_HIGH)                   /* USB charging mode control */

GPIO(PCH_RCIN_L,        PIN(110), GPIO_ODR_HIGH)                   /* Reset line to PCH (for 8042 emulation) */
GPIO(NC_115,            PIN(115), GPIO_INPUT | GPIO_PULL_UP)       /* NC */

GPIO(NC_122,            PIN(122), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(EC_STRAP_GPIO1,    PIN(123), GPIO_OUT_LOW)
GPIO(NC_124,            PIN(124), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(GYRO_INT2,         PIN(127), GPIO_INT_FALLING)                /* Gyro sensor interrupt 2 to EC */

GPIO(NC_132,            PIN(132), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(BAT_LED0_L,        PIN(133), GPIO_ODR_HIGH)                   /* Battery charging LED - blue */
GPIO(BOARD_VERSION2,    PIN(134), GPIO_INPUT)                      /* Board ID */
GPIO(WWAN_PWREN,        PIN(135), GPIO_OUT_HIGH)                   /* Enable power for WWAN - PROBE_DETECT_L */
GPIO(BAT_LED1_L,        PIN(136), GPIO_ODR_HIGH)                   /* Battery charging LED - orange */

GPIO(NC_140,            PIN(140), GPIO_INPUT | GPIO_PULL_UP)       /* NC */
GPIO(PWR_LED0_L,        PIN(141), GPIO_ODR_HIGH)                   /* Power LED - blue */
GPIO(PCH_RSMRST_L,      PIN(143), GPIO_OUT_LOW)                    /* RSMRST_N to PCH */
GPIO(PWR_LED1_L,        PIN(145), GPIO_ODR_HIGH)                   /* Power LED - orange */
GPIO(PVT_CS0,           PIN(146), GPIO_ODR_HIGH)                   /* SPI PVT Chip select */
GPIO(EC_KBD_ALERT,      PIN(147), GPIO_OUT_HIGH)

GPIO(WLAN_OFF_L,        PIN(150), GPIO_ODR_HIGH)                   /* Wireless LAN */
GPIO(CPU_PROCHOT,       PIN(151), GPIO_OUT_LOW)
GPIO(KBD_IRQ_L,         PIN(152), GPIO_ODR_HIGH)                   /* Negative edge triggered irq. */
GPIO(BOARD_VERSION1,    PIN(154), GPIO_INPUT)                      /* Board ID */
GPIO(CORE_PWROK,        PIN(155), GPIO_ODR_HIGH)                   /* CORE_PWR_OK_R */
GPIO(LID_OPEN2,         PIN(156), GPIO_INT_BOTH)                   /* LID_OPEN_OUT2_R */
GPIO(PCH_SUSPWRDNACK,   PIN(157), GPIO_INT_FALLING)                /* PMC SUSPWRDNACK signal from SOC to EC */

GPIO(PCH_PWRBTN_L,      PIN(160), GPIO_OUT_HIGH)                   /* Power button output to PCH */
GPIO(GYRO_INT1,         PIN(161), GPIO_INT_FALLING)                /* Gyro sensor interrupt 1 to EC */
GPIO(NC_163,            PIN(163), GPIO_INPUT | GPIO_PULL_UP)       /* NC */

GPIO(STARTUP_LATCH_SET, PIN(201), GPIO_OUT_HIGH)
GPIO(EC_BL_DISABLE_L,   PIN(202), GPIO_OUT_HIGH)                   /* EDP backligh disable signal from EC */
GPIO(SMC_SHUTDOWN,      PIN(203), GPIO_OUT_LOW)                    /* Shutdown signal from EC to power sequencing PLD */
GPIO(NC_204,            PIN(204), GPIO_INPUT | GPIO_PULL_UP)       /* NC */

GPIO(SUSPWRDNACK_SOC_EC,PIN(210), GPIO_OUT_LOW)                    /* SUSPWRDNACK signal from EC to MOIC device */
GPIO(PCH_SLP_SX_L,      PIN(211), GPIO_INT_BOTH)                   /* Sleep SOIX signal from SOC to EC */

/* Alternate functions GPIO definition */
ALTERNATE(PIN_MASK(16, 0x24),   1,      MODULE_UART,            0)                      /* UART0 */

ALTERNATE(PIN_MASK(1,  0x60),   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0: Battery Charger */
ALTERNATE(PIN_MASK(2,  0x3f),   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C1: Motion Sensor / I2C2: SOC / I2C3: Temp Sensor */

ALTERNATE(PIN_MASK(0,  0xfc),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1,  0x03),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(10, 0xd8),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
 GPIO(KBD_KSO2,         PIN(1),      GPIO_KB_OUTPUT_COL2)                   /* Negative edge triggered irq. */
#else
 ALTERNATE(PIN_MASK(0, 0x02),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT_COL2)
#endif

ALTERNATE(PIN_MASK(3,  0x04),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(4,  0x0d),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(12, 0x60),   2,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(14, 0x14),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)

ALTERNATE(PIN_MASK(1,  0x10),   1,      MODULE_LPC,             0)                      /* 14: CLKRUN# */
ALTERNATE(PIN_MASK(11, 0x9e),   1,      MODULE_LPC,             0)                      /* 111~114: LAD[0:3], 117: PCI_CLK */
ALTERNATE(PIN_MASK(11, 0x40),   1,      MODULE_LPC,             GPIO_INT_BOTH)          /* 116: LRESET# */
ALTERNATE(PIN_MASK(12, 0x01),   1,      MODULE_LPC,             0)                      /* 120: LFRAME# */

ALTERNATE(PIN_MASK(5,  0x10),   1,      MODULE_SPI,             0)                      /* 54: MOSI */
ALTERNATE(PIN_MASK(16, 0x10),   1,      MODULE_SPI,             0)                      /* 164: MISO */
ALTERNATE(PIN_MASK(15, 0x08),   1,      MODULE_SPI,             0)                      /* 153: CLK */

/* Re-Config LPC Pins to GPIO Open Drain for SOC G3 (EC - POWER_G3) state */
ALTERNATE(PIN_MASK(1,  0x10),   0,      MODULE_GPIO,            GPIO_ODR_HIGH)          /* 14: LPC CLKRUN */
ALTERNATE(PIN_MASK(11, 0x9e),   0,      MODULE_GPIO,            GPIO_ODR_HIGH)          /* 111~114:LAD[0:3], 117:PCI_CLK */
ALTERNATE(PIN_MASK(11, 0x40),   0,      MODULE_GPIO,            GPIO_ODR_HIGH)          /* 116: LRESET# */
ALTERNATE(PIN_MASK(12, 0x01),   0,      MODULE_GPIO,            GPIO_ODR_HIGH)          /* 120: LFRAME# */
