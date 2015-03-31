/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(PCH_SLP_SUS_L,     PIN(12), GPIO_INT_BOTH | GPIO_PULL_UP,              power_signal_interrupt) /* Deep sleep state */
GPIO_INT(LID_OPEN,          PIN(27), GPIO_INT_BOTH_DSLEEP,                      lid_interrupt)          /* Lid switch */
GPIO_INT(AC_PRESENT,        PIN(30), GPIO_INT_BOTH_DSLEEP,                      extpower_interrupt)     /* BC_ACOK / EC_ACIN - to know if battery or AC connected */
GPIO_INT(VOLUME_UP_L,       PIN(31), GPIO_INT_BOTH | GPIO_PULL_UP,              button_interrupt)       /* Volume up button */
GPIO_INT(WP_L,              PIN(33), GPIO_INT_BOTH,                             switch_interrupt)       /* Write protect input */
GPIO_INT(POWER_BUTTON_L,    PIN(35), GPIO_INT_BOTH_DSLEEP,                      power_button_interrupt) /* Power button */
GPIO_INT(VOLUME_DOWN_L,     PIN(47), GPIO_INT_BOTH | GPIO_PULL_UP,              button_interrupt)       /* Volume down button */
GPIO_INT(PMIC_INT,          PIN(50), GPIO_INT_FALLING,                          power_signal_interrupt) /* Alert signal from PMIC */
GPIO_INT(PD_MCU_INT,        PIN(122),GPIO_INT_RISING | GPIO_INT_DSLEEP,         pd_mcu_interrupt)       /* Interrupt from USB PD Controller to EC */
GPIO_INT(RSMRST_L_PGOOD,    PIN(63), GPIO_INT_BOTH,                             power_signal_interrupt) /* RSMRST_N_PWRGD from power logic */
GPIO_INT(USB_C1_VBUS_WAKE,  PIN(123), GPIO_INT_BOTH,                             vbus0_evt)              /* USB_C1_VBUS WAKE EVENT DETECT */
GPIO_INT(USB_C0_BC12_INT_L, PIN(124), GPIO_INT_FALLING,                          usb0_evt)               /* USB_C0_BC12_INT */
#if defined(CONFIG_ACCEL_INTERRUPTS) && defined(CONFIG_SHARED_MOTION_INTERRUPT_LINE)
GPIO_INT(GYRO_INT2,         PIN(127), GPIO_INT_RISING | GPIO_PULL_DOWN,          motion_sns_interrupt)   /* Gyro sensor interrupt 2 to EC */
#else
GPIO_INT(GYRO_INT2,         PIN(127), GPIO_INT_RISING | GPIO_PULL_DOWN,          NULL)                   /* Gyro sensor interrupt 2 to EC */
#endif
GPIO_INT(USB_C1_BC12_INT_L, PIN(145), GPIO_INT_FALLING, usb1_evt)                                        /* USB_C1_BC12_INT */
#ifdef CONFIG_ALS_INTERRUPTS
GPIO_INT(ALS_INT,           PIN(147), GPIO_INT_FALLING | GPIO_PULL_UP,           als_interrupt)          /* ALS sensor interrupt to EC */
#else
GPIO_INT(ALS_INT,           PIN(147), GPIO_INT_FALLING | GPIO_PULL_UP,           NULL)                   /* ALS sensor interrupt to EC */
#endif
GPIO_INT(USB_C0_VBUS_WAKE,  PIN(152), GPIO_INT_BOTH,                             vbus1_evt)              /* USBC PORT 0 VBUS POWER DETECT */
#if defined(CONFIG_ACCEL_INTERRUPTS) && defined(CONFIG_SHARED_MOTION_INTERRUPT_LINE)
GPIO_INT(GYRO_INT1,         PIN(161), GPIO_INT_RISING | GPIO_PULL_DOWN,          motion_sns_interrupt)   /* Gyro sensor interrupt 1 to EC */
#else
GPIO_INT(GYRO_INT1,         PIN(161), GPIO_INT_RISING | GPIO_PULL_DOWN,          NULL)                   /* Gyro sensor interrupt 1 to EC */
#endif
GPIO_INT(PCH_SLP_S4_L,      PIN(200), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,      PIN(206), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S3# signal from PCH */
GPIO_INT(PCH_SLP_S0_L,      PIN(211), GPIO_INT_BOTH | GPIO_PULL_UP,              power_signal_interrupt) /* Sleep SO signal from SOC to EC */

GPIO(EC_BRD_ID0,        PIN(6), GPIO_INPUT)                    /* EC_BRD_ID0 */
GPIO(EC_BRD_ID1,        PIN(7), GPIO_INPUT)                    /* EC_BRD_ID1 */

GPIO(EC_BRD_ID2,          PIN(10), GPIO_INPUT)                    /* EC_BRD_ID2 */
GPIO(PP1800_DX_SENSOR_EN, PIN(11), GPIO_OUT_LOW)
GPIO(USB2_OTG_ID,         PIN(13), GPIO_ODR_HIGH)                 /* Universal Serial Bus On-The-Go detection signal */
GPIO(I2C_PORT0_SCL,       PIN(15), GPIO_ODR_HIGH)
GPIO(I2C_PORT0_SDA,       PIN(16), GPIO_ODR_HIGH)

GPIO(I2C_PORT2_SCL,     PIN(20), GPIO_ODR_HIGH)
GPIO(I2C_PORT2_SDA,     PIN(21), GPIO_ODR_HIGH)
GPIO(I2C_PORT1_SCL,     PIN(22), GPIO_ODR_HIGH)
GPIO(I2C_PORT1_SDA,     PIN(23), GPIO_ODR_HIGH)
GPIO(I2C_PORT3_SCL,     PIN(24), GPIO_ODR_HIGH)
GPIO(I2C_PORT3_SDA,     PIN(25), GPIO_ODR_HIGH)
GPIO(PCH_SCI_L,         PIN(26), GPIO_ODR_HIGH)                 /* SCI output */

GPIO(NC_034,            PIN(34), GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(USB_P3_PWR_EN,     PIN(36), GPIO_OUT_HIGH)                 /* Enable power for USB2 Port */

GPIO(ENTERING_RW,       PIN(41), GPIO_OUT_LOW)                  /* Indicate when EC is entering RW code */
GPIO(PCH_SMI_L,         PIN(44), GPIO_ODR_HIGH)                 /* SMI output */
GPIO(PCH_PWRBTN_L,      PIN(45), GPIO_OUT_HIGH)                 /* Power button output to PCH */
GPIO(DDI1_HPD,          PIN(46), GPIO_OUT_HIGH)                 /* DP hot plug detect from EC to SOC */

GPIO(DDI2_HPD,          PIN(51), GPIO_OUT_HIGH)                 /* DP hot plug detect from EC to SOC */
GPIO(CPU_PROCHOT,       PIN(52), GPIO_OPEN_DRAIN)
GPIO(ENABLE_TOUCHPAD,   PIN(53), GPIO_OUT_HIGH)                 /* Enable power to Track Pad */
GPIO(ROP_DSW_PWROK,     PIN(55), GPIO_INPUT)                    /* ZERO OHM DNP - RESERVED OPEN DRAIN */
GPIO(BC_BATPRES,        PIN(56), GPIO_INPUT)                    /* ?? */
GPIO(V_BOOSTIN,         PIN(57), GPIO_INPUT)                    /* ?? */

GPIO(USBPD_WAKE,        PIN(60), GPIO_OUT_LOW)                  /* USBC WAKE NOTIFICATION from MEC EC to STM32 EC */
GPIO(BC_IBAT,           PIN(61), GPIO_INPUT)                    /* ?? */
GPIO(BC_MON,            PIN(62), GPIO_INPUT)                    /* ADC FUNCTION - FROM BATTERY CHARGER FOR SYSTEM POWER MONITOR */
GPIO(EN_USB_C0_CHARGE,  PIN(64), GPIO_OUT_HIGH)                 /* Enable USB-C0 Charge */
GPIO(FLASH_DSC_OVERRIDE,PIN(65), GPIO_ODR_HIGH)                 /* Flash Descriptor Override */
GPIO(PCH_WAKE_L,        PIN(66), GPIO_ODR_HIGH)                 /* PCH wake pin */
GPIO(USB_P4_PWR_EN,     PIN(67), GPIO_OUT_HIGH)                 /* Enable power for USB3 Port */

GPIO(KBD_KSO2,          PIN(101), GPIO_KB_OUTPUT_COL2)           /* Negative edge triggered irq. */
GPIO(FAN_TACH,          PIN(105), GPIO_INPUT)                    /* ?? Need Alternate - FAN TACH */

GPIO(AC_PRESENT_PCH,    PIN(110), GPIO_ODR_HIGH)                 /*  */

GPIO(SYS_RESET_L,       PIN(121), GPIO_ODR_HIGH)                 /* PM_SYSRST - ODR High */
GPIO(USB_PD_EC_INT,     PIN(122), GPIO_INT_BOTH)                 /* Interrupt from USB PD Controller to EC */

GPIO(EC_USBPD_RST,      PIN(130), GPIO_ODR_HIGH)                 /* EC_USBPD_RST */
GPIO(WLAN_OFF_L,        PIN(132), GPIO_ODR_HIGH)                 /* Wireless LAN */
GPIO(WWAN_PWR_EN,       PIN(133), GPIO_OUT_HIGH)                 /* Enable power for WWAN */
GPIO(PCH_RCIN_L,        PIN(135), GPIO_ODR_HIGH)                 /* Reset line to PCH (for 8042 emulation) */

GPIO(USB2_OTG_VBUSSENSE,PIN(140), GPIO_ODR_HIGH)                 /* OTG VBUS STATUS TO PCH */
GPIO(SEN_INT,           PIN(141), GPIO_INT_BOTH_DSLEEP)          /* SEN_INT */
GPIO(PCH_RSMRST_L,      PIN(143), GPIO_OUT_LOW)                  /* RSMRST_N to PCH */
GPIO(PVT_CS0,           PIN(146), GPIO_ODR_HIGH)                 /* SPI PVT Chip select */

GPIO(NC_150,            PIN(150), GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(DEVICE_PROCHOT,    PIN(151), GPIO_INT_FALLING)              /* Device Proc Hot */
GPIO(EN_USB_C0_5V_OUT,  PIN(154), GPIO_OUT_LOW)                  /* Enable USB-C0 5V */
GPIO(CHARGE_LED1,       PIN(155), GPIO_OUT_HIGH)                 /* Charge LED 1 */
GPIO(CHARGE_LED2,       PIN(156), GPIO_OUT_HIGH)                 /* Charge LED 2 */
GPIO(EN_USB_C1_CHARGE,  PIN(157), GPIO_OUT_HIGH)                 /* Enable USB-C1 Charge */

GPIO(PP1800_DX_AUDIO_EN,PIN(160), GPIO_OUT_LOW)
GPIO(RTC_RST,           PIN(163), GPIO_OPEN_DRAIN)               /* RTC_RST */

GPIO(NC_201,            PIN(201), GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(EC_BL_DISABLE_L,   PIN(202), GPIO_OUT_HIGH)                 /* EDP backligh disable signal from EC */
GPIO(PP3300_WLAN_EN,    PIN(203), GPIO_OUT_HIGH)                 /* Enable power to wifi */
GPIO(EN_USB_C1_5V_OUT,  PIN(204), GPIO_OUT_LOW)                  /* Enable USB-C1 5V */

GPIO(ROP_EC_1HZ_WAKE,   PIN(210), GPIO_INT_BOTH)                 /* 1 HZ CLOCK FOR LOW POWER MODE FUNCTION */

/* Alternate functions GPIO definition */
ALTERNATE(PIN_MASK(16, 0x24),   1,      MODULE_UART,            0)                      /* UART0 */

ALTERNATE(PIN_MASK(1,  0x60),   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0: Battery Charger */
ALTERNATE(PIN_MASK(2,  0x3f),   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C1: Temp Sensor / I2C2: SOC / I2C3: VNN */
/* PORT SEL [3:0] has to be set to 1 to access this port 1 of controller 0 */
ALTERNATE(PIN_MASK(13, 0x10),   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0 - Port 1 : Accel SCL  */
ALTERNATE(PIN_MASK(1,  0x80),   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0 - Port 1 : Accel SDA*/

ALTERNATE(PIN_MASK(0,  0x3d),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1,  0x02),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(10, 0xdd),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(3,  0x04),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(4,  0x0d),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(12, 0x60),   2,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(14, 0x14),   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)

ALTERNATE(PIN_MASK(1,  0x10),   1,      MODULE_LPC,             0)                      /* 14: CLKRUN# */
ALTERNATE(PIN_MASK(11, 0x9e),   1,      MODULE_LPC,             0)                      /* 111~114: LAD[0:3], 117: PCI_CLK */
ALTERNATE(PIN_MASK(11, 0x40),   1,      MODULE_LPC,             GPIO_INT_BOTH)          /* 116: LRESET# */
ALTERNATE(PIN_MASK(12, 0x01),   1,      MODULE_LPC,             0)                      /* 120: LFRAME# */
ALTERNATE(PIN_MASK(11, 0x20),   1,      MODULE_LPC,             0)

ALTERNATE(PIN_MASK(5,  0x10),   1,      MODULE_SPI,             0)                      /* 54: MOSI */
ALTERNATE(PIN_MASK(16, 0x10),   1,      MODULE_SPI,             0)                      /* 164: MISO */
ALTERNATE(PIN_MASK(15, 0x08),   1,      MODULE_SPI,             0)                      /* 153: CLK */

ALTERNATE(PIN_MASK(13, 0x40),   1,      MODULE_PWM_LED,         GPIO_OPEN_DRAIN)        /* 136: PWM1 */
