/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */

GPIO_INT(AC_PRESENT,         PIN(7,  3), GPIO_INT_BOTH,                   extpower_interrupt)      /* B39 - PS2_CLK3/TA2for EC_ACIN */
GPIO_INT(LID_OPEN,           PIN(9,  5), GPIO_INT_BOTH | GPIO_PULL_UP,    lid_interrupt)           /* A47 - SPIP_MISO for LID_OPEN_D */
GPIO_INT(POWER_BUTTON_L,     PIN(9,  7), GPIO_INT_BOTH,                   power_button_interrupt)  /* A48 - GPIO97 for PWR_BTN_N */
GPIO_INT(TABLET_MODE_L,      PIN(E,  7), GPIO_INT_BOTH | GPIO_PULL_UP,    tablet_mode_interrupt)   /* B37 - 32KCLKIN for TABLET_MODE_EC */
GPIO_INT(VOLUME_UP_L,        PIN(0,  0), GPIO_INT_BOTH | GPIO_PULL_UP,    button_interrupt)        /* B68 - GPIO00 for VOLUME_UP_N */
GPIO_INT(VOLUME_DOWN_L,      PIN(7,  0), GPIO_INT_BOTH | GPIO_PULL_UP,    button_interrupt)        /* B36 - PS2_DAT0 for VOLUME_DOWN_N */
GPIO_INT(WP_L,               PIN(7,  1), GPIO_INT_BOTH,                   switch_interrupt)        /* B38 - PS2_DAT3/TB2 for SCREW_SPI_WP_N */

GPIO_INT(PCH_SLP_S4_L,       PIN(5,  0), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A25 - GPIO50 for PMU_SLP_S4_N */
GPIO_INT(PCH_SLP_S3_L,       PIN(4,  0), GPIO_INT_BOTH,                   power_signal_interrupt)  /* B21 - TA1 for PMU_SLP_S3_N */
GPIO_INT(PCH_SLP_S0_L,       PIN(3,  4), GPIO_INT_BOTH,                   power_signal_interrupt)  /* B18 - GPIO34 for PMU_SLP_S0_N */
GPIO_INT(PCH_SUS_STAT_L,     PIN(C,  5), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A61 - KBRST# for PMU_SUS_STAT_N */
GPIO_INT(PCH_SUSPWRDNACK,    PIN(B,  1), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A54 - KSO17 for SOC_SUSPWRDNACK_R */
GPIO_INT(ALL_SYS_PGOOD,      PIN(6,  2), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A31 - PS2_CLK1 for ALL_SYS_PWRGD */
GPIO_INT(RSMRST_L_PGOOD,     PIN(9,  3), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A46 - TA1/F_DIO2 for PMIC_RSMRST_N */

GPIO_INT(USB_C0_PD_INT,      PIN(8,  5), GPIO_INT_RISING,                 tcpc_alert_event)        /* A43 - RXD for USB_C0_PHY_INT */
GPIO_INT(USB_C1_PD_INT_L,    PIN(9,  4), GPIO_INT_FALLING,                tcpc_alert_event)        /* B49 - GPIO94 for USB_C1_PHY_INT_N */
GPIO_INT(CHARGER_INT_L,      PIN(0,  2), GPIO_INT_FALLING | GPIO_PULL_UP, bd99955_vbus_interrupt)  /* A01 - GPIO02 for CHRGR_INT_N */

GPIO(NC_USB_C0_VBUS_WAKE_L,  PIN(A,  7), GPIO_INPUT | GPIO_PULL_UP)       /* B56 - PS2_DAT3/TB2/F_DIO3 for USB_C0_VBUS_DET_N */
GPIO(NC_USB_C1_VBUS_WAKE_L,  PIN(6,  1), GPIO_INPUT | GPIO_PULL_UP)       /* B32 - GPIO61 for USB_C1_VBUS_DET_N */

/* Board ID */
GPIO(BOARD_VERSION1,         PIN(0,  4), GPIO_INPUT)                      /* A02 - KSO13 for EC_BRD_ID1 */
GPIO(BOARD_VERSION2,         PIN(8,  2), GPIO_INPUT)                      /* B43 - KSO14 for EC_BRD_ID2 */
GPIO(BOARD_VERSION3,         PIN(8,  3), GPIO_INPUT)                      /* B44 - KSO15 for EC_BRD_ID3 */

/* I2C pins - these will be reconfigured for alternate function below */
GPIO(I2C0_0_SCL,             PIN(B,  5), GPIO_INPUT)                      /* A56 - I2C0_SCL0 for EC_ROP_I2C_CLK */
GPIO(I2C0_0_SDA,             PIN(B,  4), GPIO_INPUT)                      /* B59 - I2C0_SDA0 for EC_ROP_I2C_SDA */
GPIO(I2C0_1_SCL,             PIN(B,  3), GPIO_INPUT)                      /* A55 - I2C0_SCL1 for EC_MUX_CLK */
GPIO(I2C0_1_SDA,             PIN(B,  2), GPIO_INPUT)                      /* B58 - I2C0_SDA1 for EC_MUX_SDA */
GPIO(I2C1_SCL,               PIN(9,  0), GPIO_INPUT)                      /* B47 - I2C1_SCL0 for EC_PD_I2C_CLK */
GPIO(I2C1_SDA,               PIN(8,  7), GPIO_INPUT)                      /* A44 - I2C1_SDA0 for EC_PD_I2C_SDA */
GPIO(I2C2_SCL,               PIN(9,  2), GPIO_INPUT)                      /* B48 - I2C2_SCL0 for EC_SENSOR_I2C_CLK */
GPIO(I2C2_SDA,               PIN(9,  1), GPIO_INPUT)                      /* A45 - I2C2_SDA0 for EC_SENSOR_I2C_SDA */
GPIO(I2C3_SCL,               PIN(D,  1), GPIO_INPUT)                      /* A63 - I2C3_SCL0 for EC_SMB_CLK */
GPIO(I2C3_SDA,               PIN(D,  0), GPIO_INPUT)                      /* B66 - I2C3_SDA0 for EC_SMB_SDA */

/*
 * TODO(crosbug.com/p/40848): These LEDs should be under control of the npcx
 * LED control unit. Remove these GPIO definitions once the LED control unit
 * is functional.
 */
/* LED */
GPIO(BAT_LED_BLUE,           PIN(B,  7), GPIO_OUT_LOW)                    /* A57 - PWM5 for CHARGE_LED1 */
GPIO(BAT_LED_AMBER,          PIN(C,  0), GPIO_OUT_LOW)                    /* B61 - PWM6 for CHARGE_LED2 */

/* Power Control*/
GPIO(V5A_EN,                 PIN(C,  4), GPIO_OUT_LOW)                    /* B64 - PWM2 for V5A_EN */
GPIO(G3_SLEEP_EN,            PIN(E,  3), GPIO_OUT_LOW)                    /* B51 - GPIOE3 for G3_SLEEP_ENA */
GPIO(ENABLE_BACKLIGHT,       PIN(E,  2), GPIO_OUT_LOW)                    /* A41 - JTAG_TDI1 for EC_BL_DISABLE_N */
GPIO(ENABLE_TOUCHPAD,        PIN(A,  6), GPIO_OUT_LOW)                    /* B55 - PS2_CLK3/TA2/F_CS1# for TRACKPAD_PWR_EN */
GPIO(WLAN_OFF_L,             PIN(E,  0), GPIO_OUT_LOW)                    /* A24 - GPOE0 for WLAN_OFF_N */
GPIO(WLAN_EN,                PIN(E,  4), GPIO_OUT_LOW)                    /* A52 - GPIOE4 for EN_PWR_WIFI */

/* Sensors Interrupt */
GPIO(ALS_INT,                PIN(3,  3), GPIO_INPUT)                      /* B17 - GPIO33 for ALS_IRQ_N */
GPIO(BASE_ACCEL_INT,         PIN(A,  3), GPIO_INPUT)                      /* A50 - SPIP_MOSI for BASE_ACCEL_INT */
GPIO(GYRO_INT,               PIN(8,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B46 - TXD/F_CS1# for GYRO_INT_N */

/* SOC */
GPIO(PCH_SCI_L,              PIN(7,  6), GPIO_ODR_HIGH)                   /* A38 - SCI# for EC_SCI_3P3_N */
GPIO(PCH_SMI_L,              PIN(C,  6), GPIO_ODR_HIGH)                   /* B65 - SMI# for EC_SMI_3P3_N */
GPIO(PCH_PWRBTN_L,           PIN(A,  5), GPIO_ODR_HIGH)                   /* A51 - A20M for EC_SOC_PWRBTN_N */
GPIO(CPU_PROCHOT,            PIN(D,  7), GPIO_OUT_LOW)                    /* A18 - GPIOD7 for EC_SOC_PROCHOT */
/* When asserted, ME does not lock security descriptor */
GPIO(PCH_SEC_DISABLE_L,      PIN(6,  3), GPIO_OUT_HIGH)                   /* B33 - PS2_DAT1 for FLASH_DSC_OVERRIDE_N */
GPIO(PCH_WAKE_L,             PIN(3,  5), GPIO_OUT_HIGH)                   /* A17 - GPIO35/TEST# for EC_SOC_WAKE_N */
GPIO(PCH_RCIN_L,             PIN(E,  1), GPIO_OUT_HIGH)	                  /* B30 - GPIOE1 for EC_SOC_RST_OUT_N */
GPIO(KBD_IRQ_L_DO_NOT_USE,   PIN(D,  5), GPIO_INPUT | GPIO_PULL_UP)       /* A08 - JTAG_TCK1 for EC_KBD_IRQ_N_R */
GPIO(PCH_SYS_PWROK,          PIN(7,  2), GPIO_OUT_LOW)                    /* A36 - PWRGD for SOC_PWROK_R */

GPIO(PCH_RSMRST_L,           PIN(8,  4), GPIO_OUT_LOW)                    /* B45 - GPIO84 for EC_RSMRST_OUT_N */
GPIO(PCH_RTCRST,             PIN(C,  1), GPIO_OUT_LOW)                    /* A58 - GPIOC1 for EC_RTCRST */

/* USB-A Port */
GPIO(USB1_ENABLE,            PIN(6,  7), GPIO_OUT_LOW)                    /* A33 - PS2_CLK0 for USBPD_MB_PWR_EN */
GPIO(USB2_ENABLE,            PIN(D,  4), GPIO_OUT_LOW)                    /* B08 - JTAG_TDO1 for USBPD_DB_PWR_EN */
GPIO(USB1_ILIM_SEL,          PIN(A,  1), GPIO_ODR_HIGH)                   /* A49 - SPIP_SCLK for USBPD_MBILIMSEL_DBSTATUS_N */
GPIO(USB2_ILIM_SEL,          PIN(C,  2), GPIO_ODR_HIGH)                   /* A59 - PWM1 for USBPD_MBSTATUS_N_DBILIMSEL */
GPIO(USB_CTL1,               PIN(B,  6), GPIO_ODR_HIGH)                   /* B60 - PWM4/Eng_Strap# for EC_GPOB6 */

/* USB-C Port 0 */
GPIO(USB_C0_5V_EN,           PIN(0,  1), GPIO_OUT_LOW)                    /* B01 - GPIO01 for EN_USB_C0_5V_OUT */
GPIO(USB_C0_RST_L,           PIN(7,  4), GPIO_OUT_LOW)                    /* A37 - GPIO74 for USB_C0_RESET_ANX_N */
GPIO(USB_C0_PWR_EN,          PIN(D,  3), GPIO_OUT_LOW)                    /* A64 - TB1 for USB_C0_ANX_PWR_EN */
GPIO(USB_C0_CABLE_DET,       PIN(B,  0), GPIO_INPUT)                      /* B57 - GPIOB0 for USB_C0_CABLE_DET */

/* USB-C Port 1 */
GPIO(USB_C1_5V_EN,           PIN(E,  5), GPIO_OUT_LOW)                    /* B62 - JTAG_TMS1 for EN_USB_C1_5V_OUT */
GPIO(USB_C1_RST_L,           PIN(C,  7), GPIO_OUT_LOW)                    /* A62 - GPIOC7 for USC_C1_RESET_PAR_N */

/* Other EC Pins */
GPIO(ENTERING_RW,            PIN(6,  0), GPIO_OUT_LOW)                    /* A30 - PWM7 for EC_ENTERING_RW */
GPIO(BAT_PRESENT_L,          PIN(C,  3), GPIO_INPUT | GPIO_PULL_UP)       /* A60 - PWM0 for BC_BATPRES_N */
GPIO(PLATFORM_EC_PROCHOT,    PIN(3,  6), GPIO_INPUT)                      /* B19 - GPIO36 for SOURCE_EC_PROCHOT_R */
GPIO(THERM_OVERTEMP_L,       PIN(8,  0), GPIO_INPUT | GPIO_PULL_UP)       /* A39 - PWM3 for THERM_OVERTEMP_N */
GPIO(USB2_OTG_ID,            PIN(7,  5), GPIO_ODR_LOW)                    /* B40 - 32KHZ_OUT for USB2_OTG_ID */
GPIO(USB2_OTG_VBUSSENSE,     PIN(D,  2), GPIO_ODR_LOW)                    /* B67 - GPIOD2 for USB_VBUSSNS */

/* NC pins - Set as input and pull them up for better power consumption */
GPIO(NC_003,                 PIN(0,  3), GPIO_INPUT | GPIO_PULL_UP)       /* B02 - KSO16 for TP_PMIC_EN */
GPIO(NC_032,                 PIN(3,  2), GPIO_INPUT | GPIO_PULL_UP)       /* A16 - TRIS# for TP_EC_GPO32 */
GPIO(NC_037,                 PIN(3,  7), GPIO_INPUT | GPIO_PULL_UP)       /* B20 - PS2_CLK2 for DDI2_HPD */
GPIO(NC_066,                 PIN(6,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B35 - ARM#_x86 for DDI1_HPD */
GPIO(NC_081,                 PIN(8,  1), GPIO_INPUT | GPIO_PULL_UP)       /* A40 - Unused pin */
GPIO(NC_0D6,                 PIN(D,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B09 - Unused pin */

/* Keyboard inverted COL2 */
GPIO(KBD_KSO2,               PIN(1,  7), GPIO_OUT_LOW)                    /* B11 - KSO02 for KB_ROW_2 */

UNIMPLEMENTED(UNIMPLEMENTED)

/* Alternate functions GPIO definitions */
/* ADC pins */
ALTERNATE(PIN_MASK(4, 0x3E), 1,  MODULE_ADC,       0)   /* ADC0/ADC1/ADC2/ADC3/ADC4 for ADC GPIO45/44/43/42/41 */

/* I2C pins */
ALTERNATE(PIN_MASK(B, 0x0C), 1,  MODULE_I2C,       0)   /* I2C0SDA1/I2C0SCL1 for I2C GPIOB2/B3	   */
ALTERNATE(PIN_MASK(B, 0x30), 1,  MODULE_I2C,       0)   /* I2C0SDA0/I2C0SCL0 for I2C GPIOB4/B5	   */
ALTERNATE(PIN_MASK(8, 0x80), 1,  MODULE_I2C,       0)   /* I2C1SDA for I2C GPIO87 */
ALTERNATE(PIN_MASK(9, 0x07), 1,  MODULE_I2C,       0)   /* I2C1SCL/I2C2SDA/I2C2SCL for I2C GPIO90/91/92 */
ALTERNATE(PIN_MASK(D, 0x03), 1,  MODULE_I2C,       0)   /* I2C3SDA/I2C3SCL for I2C GPIOD0/D1    */

/* Keyboard Columns (except inverted COL2/GP17) */
ALTERNATE(PIN_MASK(0, 0xE0), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(1, 0x7F), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(2, 0x03), 0,  MODULE_KEYBOARD_SCAN, 0)

/* Keyboard Rows */
ALTERNATE(PIN_MASK(2, 0xFC), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(3, 0x03), 0,  MODULE_KEYBOARD_SCAN, 0)

/* LED1 - GPIOB7. LED2 - GPIOC0 */
ALTERNATE(PIN_MASK(B, 0x80), 3,  MODULE_POWER_LED, 0)   /* PWM5 for CHARGE_LED1 */
ALTERNATE(PIN_MASK(C, 0x01), 3,  MODULE_POWER_LED, 0)   /* PWM6 for CHARGE_LED2 */

/* SPI pins */
ALTERNATE(PIN_MASK(A, 0x0A), 1,  MODULE_SPI,       0)   /* SPIP_MOSI/SPIP_SCLK for SPI GPIOA3/A1 */
ALTERNATE(PIN_MASK(9, 0x20), 1,  MODULE_SPI,       0)   /* SPIP_MISO for SPI GPIO95 */

/* UART pins */
ALTERNATE(PIN_MASK(6, 0x30), 1,  MODULE_UART,      0)   /* CR_SIN/SOUT for UART GPIO64/65 */
