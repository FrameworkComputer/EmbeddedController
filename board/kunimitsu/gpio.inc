/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(PCH_SLP_SUS_L,     PORT(1),  2, GPIO_INT_BOTH | GPIO_PULL_UP,              power_signal_interrupt) /* Deep sleep state */
GPIO_INT(LID_OPEN,          PORT(2),  7, GPIO_INT_BOTH_DSLEEP,                      lid_interrupt)          /* Lid switch */
GPIO_INT(AC_PRESENT,        PORT(3),  0, GPIO_INT_BOTH_DSLEEP,                      extpower_interrupt)     /* BC_ACOK / EC_ACIN - to know if battery or AC connected */
GPIO_INT(VOLUME_UP_L,       PORT(3),  1, GPIO_INT_BOTH | GPIO_PULL_UP,              button_interrupt)       /* Volume up button */
GPIO_INT(WP_L,              PORT(3),  3, GPIO_INT_BOTH,                             switch_interrupt)       /* Write protect input */
GPIO_INT(POWER_BUTTON_L,    PORT(3),  5, GPIO_INT_BOTH_DSLEEP,                      power_button_interrupt) /* Power button */
GPIO_INT(VOLUME_DOWN_L,     PORT(4),  7, GPIO_INT_BOTH | GPIO_PULL_UP,              button_interrupt)       /* Volume down button */
GPIO_INT(PMIC_INT,          PORT(5),  0, GPIO_INT_FALLING,                          power_signal_interrupt) /* Alert signal from PMIC */
GPIO_INT(PD_MCU_INT,        PORT(12), 2, GPIO_INT_RISING | GPIO_INT_DSLEEP,         pd_mcu_interrupt)       /* Interrupt from USB PD Controller to EC */
GPIO_INT(RSMRST_L_PGOOD,    PORT(6),  3, GPIO_INT_BOTH,                             power_signal_interrupt) /* RSMRST_N_PWRGD from power logic */
GPIO_INT(USB_C1_VBUS_WAKE,  PORT(12), 3, GPIO_INT_BOTH,                             vbus0_evt)              /* USB_C1_VBUS WAKE EVENT DETECT */
GPIO_INT(USB_C0_BC12_INT_L, PORT(12), 4, GPIO_INT_FALLING,                          usb0_evt)               /* USB_C0_BC12_INT */
#if defined(CONFIG_ACCEL_INTERRUPTS) && defined(CONFIG_SHARED_MOTION_INTERRUPT_LINE)
GPIO_INT(GYRO_INT2,         PORT(12), 7, GPIO_INT_RISING | GPIO_PULL_DOWN,          motion_sns_interrupt)   /* Gyro sensor interrupt 2 to EC */
#else
GPIO_INT(GYRO_INT2,         PORT(12), 7, GPIO_INT_RISING | GPIO_PULL_DOWN,          NULL)                   /* Gyro sensor interrupt 2 to EC */
#endif
GPIO_INT(USB_C1_BC12_INT_L, PORT(14), 5, GPIO_INT_FALLING, usb1_evt)                                        /* USB_C1_BC12_INT */
#ifdef CONFIG_ALS_INTERRUPTS
GPIO_INT(ALS_INT,           PORT(14), 7, GPIO_INT_FALLING | GPIO_PULL_UP,           als_interrupt)          /* ALS sensor interrupt to EC */
#else
GPIO_INT(ALS_INT,           PORT(14), 7, GPIO_INT_FALLING | GPIO_PULL_UP,           NULL)                   /* ALS sensor interrupt to EC */
#endif
GPIO_INT(USB_C0_VBUS_WAKE,  PORT(15), 2, GPIO_INT_BOTH,                             vbus1_evt)              /* USBC PORT 0 VBUS POWER DETECT */
#if defined(CONFIG_ACCEL_INTERRUPTS) && defined(CONFIG_SHARED_MOTION_INTERRUPT_LINE)
GPIO_INT(GYRO_INT1,         PORT(16), 1, GPIO_INT_RISING | GPIO_PULL_DOWN,          motion_sns_interrupt)   /* Gyro sensor interrupt 1 to EC */
#else
GPIO_INT(GYRO_INT1,         PORT(16), 1, GPIO_INT_RISING | GPIO_PULL_DOWN,          NULL)                   /* Gyro sensor interrupt 1 to EC */
#endif
GPIO_INT(PCH_SLP_S4_L,      PORT(20), 0, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,      PORT(20), 6, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S3# signal from PCH */
GPIO_INT(PCH_SLP_S0_L,      PORT(21), 1, GPIO_INT_BOTH | GPIO_PULL_UP,              power_signal_interrupt) /* Sleep SO signal from SOC to EC */

GPIO(EC_BRD_ID0,        PORT(0),  6, GPIO_INPUT)                    /* EC_BRD_ID0 */
GPIO(EC_BRD_ID1,        PORT(0),  7, GPIO_INPUT)                    /* EC_BRD_ID1 */

GPIO(EC_BRD_ID2,        PORT(1),  0, GPIO_INPUT)                    /* EC_BRD_ID2 */
GPIO(PP1800_DX_SENSOR_EN,PORT(1), 1, GPIO_OUT_LOW)
GPIO(USB2_OTG_ID,       PORT(1),  3, GPIO_ODR_HIGH)                 /* Universal Serial Bus On-The-Go detection signal */
GPIO(I2C_PORT0_SCL,     PORT(1),  5, GPIO_ODR_HIGH)
GPIO(I2C_PORT0_SDA,     PORT(1),  6, GPIO_ODR_HIGH)

GPIO(I2C_PORT2_SCL,     PORT(2),  0, GPIO_ODR_HIGH)
GPIO(I2C_PORT2_SDA,     PORT(2),  1, GPIO_ODR_HIGH)
GPIO(I2C_PORT1_SCL,     PORT(2),  2, GPIO_ODR_HIGH)
GPIO(I2C_PORT1_SDA,     PORT(2),  3, GPIO_ODR_HIGH)
GPIO(I2C_PORT3_SCL,     PORT(2),  4, GPIO_ODR_HIGH)
GPIO(I2C_PORT3_SDA,     PORT(2),  5, GPIO_ODR_HIGH)
GPIO(PCH_SCI_L,         PORT(2),  6, GPIO_ODR_HIGH)                 /* SCI output */

GPIO(NC_034,            PORT(3),  4, GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(USB_P3_PWR_EN,     PORT(3),  6, GPIO_OUT_HIGH)                 /* Enable power for USB2 Port */

GPIO(ENTERING_RW,       PORT(4),  1, GPIO_OUT_LOW)                  /* Indicate when EC is entering RW code */
GPIO(PCH_SMI_L,         PORT(4),  4, GPIO_ODR_HIGH)                 /* SMI output */
GPIO(PCH_PWRBTN_L,      PORT(4),  5, GPIO_OUT_HIGH)                 /* Power button output to PCH */
GPIO(DDI1_HPD,          PORT(4),  6, GPIO_OUT_HIGH)                 /* DP hot plug detect from EC to SOC */

GPIO(DDI2_HPD,          PORT(5),  1, GPIO_OUT_HIGH)                 /* DP hot plug detect from EC to SOC */
GPIO(CPU_PROCHOT,       PORT(5),  2, GPIO_OPEN_DRAIN)
GPIO(ENABLE_TOUCHPAD,   PORT(5),  3, GPIO_OUT_HIGH)                 /* Enable power to Track Pad */
GPIO(ROP_DSW_PWROK,     PORT(5),  5, GPIO_INPUT)                    /* ZERO OHM DNP - RESERVED OPEN DRAIN */
GPIO(BC_BATPRES,        PORT(5),  6, GPIO_INPUT)                    /* ?? */
GPIO(V_BOOSTIN,         PORT(5),  7, GPIO_INPUT)                    /* ?? */

GPIO(USBPD_WAKE,        PORT(6),  0, GPIO_OUT_LOW)                  /* USBC WAKE NOTIFICATION from MEC EC to STM32 EC */
GPIO(BC_IBAT,           PORT(6),  1, GPIO_INPUT)                    /* ?? */
GPIO(BC_MON,            PORT(6),  2, GPIO_INPUT)                    /* ADC FUNCTION - FROM BATTERY CHARGER FOR SYSTEM POWER MONITOR */
GPIO(EN_USB_C0_CHARGE,  PORT(6),  4, GPIO_OUT_HIGH)                 /* Enable USB-C0 Charge */
GPIO(FLASH_DSC_OVERRIDE,PORT(6),  5, GPIO_ODR_HIGH)                 /* Flash Descriptor Override */
GPIO(PCH_WAKE_L,        PORT(6),  6, GPIO_ODR_HIGH)                 /* PCH wake pin */
GPIO(USB_P4_PWR_EN,     PORT(6),  7, GPIO_OUT_HIGH)                 /* Enable power for USB3 Port */

GPIO(KBD_KSO2,          PORT(10), 1, GPIO_KB_OUTPUT_COL2)           /* Negative edge triggered irq. */
GPIO(FAN_TACH,          PORT(10), 5, GPIO_INPUT)                    /* ?? Need Alternate - FAN TACH */

GPIO(AC_PRESENT_PCH,    PORT(11), 0, GPIO_ODR_HIGH)                 /*  */

GPIO(SYS_RESET_L,       PORT(12), 1, GPIO_ODR_HIGH)                 /* PM_SYSRST - ODR High */
GPIO(USB_PD_EC_INT,     PORT(12), 2, GPIO_INT_BOTH)                 /* Interrupt from USB PD Controller to EC */

GPIO(EC_USBPD_RST,      PORT(13), 0, GPIO_ODR_HIGH)                 /* EC_USBPD_RST */
GPIO(WLAN_OFF_L,        PORT(13), 2, GPIO_ODR_HIGH)                 /* Wireless LAN */
GPIO(WWAN_PWR_EN,       PORT(13), 3, GPIO_OUT_HIGH)                 /* Enable power for WWAN */
GPIO(PCH_RCIN_L,        PORT(13), 5, GPIO_ODR_HIGH)                 /* Reset line to PCH (for 8042 emulation) */

GPIO(USB2_OTG_VBUSSENSE,PORT(14), 0, GPIO_ODR_HIGH)                 /* OTG VBUS STATUS TO PCH */
GPIO(SEN_INT,           PORT(14), 1, GPIO_INT_BOTH_DSLEEP)          /* SEN_INT */
GPIO(PCH_RSMRST_L,      PORT(14), 3, GPIO_OUT_LOW)                  /* RSMRST_N to PCH */
GPIO(PVT_CS0,           PORT(14), 6, GPIO_ODR_HIGH)                 /* SPI PVT Chip select */

GPIO(NC_150,            PORT(15), 0, GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(DEVICE_PROCHOT,    PORT(15), 1, GPIO_INT_FALLING)              /* Device Proc Hot */
GPIO(EN_USB_C0_5V_OUT,  PORT(15), 4, GPIO_OUT_LOW)                  /* Enable USB-C0 5V */
GPIO(CHARGE_LED1,       PORT(15), 5, GPIO_OUT_HIGH)                 /* Charge LED 1 */
GPIO(CHARGE_LED2,       PORT(15), 6, GPIO_OUT_HIGH)                 /* Charge LED 2 */
GPIO(EN_USB_C1_CHARGE,  PORT(15), 7, GPIO_OUT_HIGH)                 /* Enable USB-C1 Charge */

GPIO(PP1800_DX_AUDIO_EN,PORT(16), 0, GPIO_OUT_LOW)
GPIO(RTC_RST,           PORT(16), 3, GPIO_OPEN_DRAIN)               /* RTC_RST */

GPIO(NC_201,            PORT(20), 1, GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(EC_BL_DISABLE_L,   PORT(20), 2, GPIO_OUT_HIGH)                 /* EDP backligh disable signal from EC */
GPIO(PP3300_WLAN_EN,    PORT(20), 3, GPIO_OUT_HIGH)                 /* Enable power to wifi */
GPIO(EN_USB_C1_5V_OUT,  PORT(20), 4, GPIO_OUT_LOW)                  /* Enable USB-C1 5V */

GPIO(ROP_EC_1HZ_WAKE,   PORT(21), 0, GPIO_INT_BOTH)                 /* 1 HZ CLOCK FOR LOW POWER MODE FUNCTION */

/* Alternate functions GPIO definition */
ALTERNATE(PORT(16),     0x24,   1,      MODULE_UART,            0)                      /* UART0 */

ALTERNATE(PORT(1),      0x60,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0: Battery Charger */
ALTERNATE(PORT(2),      0x3f,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C1: Temp Sensor / I2C2: SOC / I2C3: VNN */
/* PORT SEL [3:0] has to be set to 1 to access this port 1 of controller 0 */
ALTERNATE(PORT(13),     0x10,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0 - Port 1 : Accel SCL  */
ALTERNATE(PORT(1),      0x80,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0 - Port 1 : Accel SDA*/

ALTERNATE(PORT(0),      0x3d,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(1),      0x02,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(10),     0xdd,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(3),      0x04,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PORT(4),      0x0d,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PORT(12),     0x60,   2,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PORT(14),     0x14,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)

ALTERNATE(PORT(1),      0x10,   1,      MODULE_LPC,             0)                      /* 14: CLKRUN# */
ALTERNATE(PORT(11),     0x9e,   1,      MODULE_LPC,             0)                      /* 111~114: LAD[0:3], 117: PCI_CLK */
ALTERNATE(PORT(11),     0x40,   1,      MODULE_LPC,             GPIO_INT_BOTH)          /* 116: LRESET# */
ALTERNATE(PORT(12),     0x01,   1,      MODULE_LPC,             0)                      /* 120: LFRAME# */
ALTERNATE(PORT(11),     0x20,   1,      MODULE_LPC,             0)

ALTERNATE(PORT(5),      0x10,   1,      MODULE_SPI,             0)                      /* 54: MOSI */
ALTERNATE(PORT(16),     0x10,   1,      MODULE_SPI,             0)                      /* 164: MISO */
ALTERNATE(PORT(15),     0x08,   1,      MODULE_SPI,             0)                      /* 153: CLK */

ALTERNATE(PORT(13),     0x40,   1,      MODULE_PWM_LED,         GPIO_OPEN_DRAIN)        /* 136: PWM1 */
