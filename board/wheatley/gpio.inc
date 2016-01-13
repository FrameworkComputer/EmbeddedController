/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(LID_OPEN,           PIN(9,  5), GPIO_INT_BOTH | GPIO_PULL_UP,    lid_interrupt)           /* A47 - SPIP_MISO for LID_OPEN_EC */
GPIO_INT(AC_PRESENT,         PIN(B,  0), GPIO_INT_BOTH,                   extpower_interrupt)      /* B57 - GPIOB0 for EC_PCH_ACPRESENT */
GPIO_INT(WP_L,               PIN(7,  1), GPIO_INT_BOTH,                   switch_interrupt)        /* B38 - PS2_DAT3/TB2 for EC_WP_L */
/* Buffered power button input from PMIC / ROP_EC_PWR_BTN_L_R */
GPIO_INT(POWER_BUTTON_L,     PIN(9,  7), GPIO_INT_BOTH,                   power_button_interrupt)  /* A48 - GPIO97 for ROP_EC_PWR_BTN_L_R */
/* RSMRST from PMIC */
GPIO_INT(RSMRST_L_PGOOD,     PIN(7,  2), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A36 - PWRGD for ROP_EC_RSMRST_L */
GPIO_INT(PCH_SLP_S4_L,       PIN(5,  0), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A25 - GPIO50 for SLP_S4_L */
GPIO_INT(PCH_SLP_S3_L,       PIN(4,  0), GPIO_INT_BOTH,                   power_signal_interrupt)  /* B21 - TA1 for SLP_S3_L */
/*
 * This pulldown should be removed in future hardware followers. The signal
 * is pulled up in the SoC when the primary rails are on and/or ramping.
 * In order to not get interrupt storms there should be external logic
 * which makes this a true binary signal into the EC.
 */
GPIO_INT(PCH_SLP_S0_L,       PIN(3,  4), GPIO_INT_BOTH | GPIO_PULL_DOWN,  power_signal_interrupt)  /* B18 - GPIO34 for SLP_S0_L */
GPIO_INT(PCH_SLP_SUS_L,      PIN(B,  1), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A54 - KSO17 for SLP_SUS_L_PCH */
GPIO_INT(VOLUME_UP_L,        PIN(0,  0), GPIO_INT_FALLING | GPIO_PULL_UP, button_interrupt)        /* B68 - GPIO00 for VOLUME_UP_L */
GPIO_INT(VOLUME_DOWN_L,      PIN(7,  0), GPIO_INT_FALLING | GPIO_PULL_UP, button_interrupt)        /* B36 - PS2_DAT0 for VOLUME_DOWN_L */
GPIO_INT(PMIC_INT_L,         PIN(6,  2), GPIO_INT_FALLING,                power_signal_interrupt)  /* A31 - PS2_CLK1 for ROP_INT_L */
GPIO_INT(PD_MCU_INT,         PIN(0,  2), GPIO_INT_FALLING | GPIO_PULL_UP, pd_mcu_interrupt)        /* A01 - GPIO02 for USB_MCU_EC_INT */
GPIO_INT(USB_C0_VBUS_WAKE_L, PIN(A,  7), GPIO_INT_BOTH,                   vbus0_evt)               /* B56 - PS2_DAT3/TB2/F_DIO3 for USB_C0_VBUS_DET_L */
GPIO_INT(USB_C1_VBUS_WAKE_L, PIN(6,  1), GPIO_INT_BOTH,                   vbus1_evt)               /* B32 - GPIO61 for USB_C1_VBUS_DET_L */
GPIO_INT(USB_C0_BC12_INT_L,  PIN(8,  5), GPIO_INT_FALLING,                usb0_evt)                /* A43 - RXD for USB_C0_BC12_INT_L */
GPIO_INT(USB_C1_BC12_INT_L,  PIN(9,  4), GPIO_INT_FALLING,                usb1_evt)                /* B49 - GPIO94 for USB_C1_BC12_INT_L */
GPIO_INT(TABLET_MODE_L,      PIN(E,  7), GPIO_INT_BOTH | GPIO_PULL_UP,    tablet_mode_interrupt)   /* B37 - 32KCLKIN for TABLET_MODE_EC */

/* Delayed PWR_OK from PMIC */
GPIO_INT(PMIC_DPWROK,        PIN(C,  3), GPIO_INT_BOTH,                   power_signal_interrupt)  /* A60 - PWM0 for ROP_DSW_PWROK_EC */

GPIO(PD_RST_L,               PIN(7,  4), GPIO_ODR_HIGH)                   /* A37 - GPIO74 for USB_PD_RST_L */
GPIO(USB2_OTG_ID,            PIN(7,  5), GPIO_ODR_LOW)                    /* B40 - 32KHZ_OUT for USB2_OTG_ID */
/* I2C pins - these will be reconfigured for alternate function below */
GPIO(I2C0_0_SCL,             PIN(B,  5), GPIO_INPUT)                      /* A56 - I2C0_SCL0 for EC_I2C00_PMIC_TYPEC_SCL */
GPIO(I2C0_0_SDA,             PIN(B,  4), GPIO_INPUT)                      /* B59 - I2C0_SDA0 for EC_I2C00_PMIC_TYPEC_SDA */
GPIO(I2C0_1_SCL,             PIN(B,  3), GPIO_INPUT)                      /* A55 - I2C0_SCL1 for EC_I2C01_MUX_SCL */
GPIO(I2C0_1_SDA,             PIN(B,  2), GPIO_INPUT)                      /* B58 - I2C0_SDA1 for EC_I2C01_MUX_SDA */
GPIO(I2C1_SCL,               PIN(9,  0), GPIO_INPUT)                      /* B47 - I2C1_SCL0 for EC_I2C1_PD_SCL */
GPIO(I2C1_SDA,               PIN(8,  7), GPIO_INPUT)                      /* A44 - I2C1_SDA0 for EC_I2C1_PD_SDA */
GPIO(I2C2_SCL,               PIN(9,  2), GPIO_INPUT)                      /* B48 - I2C2_SCL0 for EC_I2C2_SENSOR_3V3_SCL */
GPIO(I2C2_SDA,               PIN(9,  1), GPIO_INPUT)                      /* A45 - I2C2_SDA0 for EC_I2C2_SENSOR_3V3_SDA */
GPIO(I2C3_SCL,               PIN(D,  1), GPIO_INPUT)                      /* A63 - I2C3_SCL0 for EC_I2C3_POWER_SCL */
GPIO(I2C3_SDA,               PIN(D,  0), GPIO_INPUT)                      /* B66 - I2C3_SDA0 for EC_I2C3_POWER_SDA */
GPIO(PCH_SCI_L,              PIN(7,  6), GPIO_ODR_HIGH)                   /* A38 - SCI# for EC_SCI_L */
/* KB BL PWM, only connected to TP */
GPIO(PWM_KBLIGHT,            PIN(C,  4), GPIO_OUT_LOW)                    /* B64 - PWM2 for KB_BL_PWM */
GPIO(USB1_ENABLE,            PIN(3,  2), GPIO_OUT_LOW)                    /* A16 - TRIS# for EN_USB_A0_PWR */
GPIO(USB2_ENABLE,            PIN(D,  4), GPIO_OUT_LOW)                    /* B08 - JTAG_TDO1 for EN_USB_A1_PWR */
GPIO(ENTERING_RW,            PIN(6,  0), GPIO_OUT_LOW)                    /* A30 - PWM7 for EC_ENTERING_RW */
GPIO(PCH_SMI_L,              PIN(C,  6), GPIO_ODR_HIGH)                   /* B65 - SMI# for EC_SMI_L */
GPIO(PCH_PWRBTN_L,           PIN(A,  5), GPIO_OUTPUT)                     /* A51 - A20M for EC_PCH_PWR_BTN_L */
GPIO(USB_C0_DP_HPD,          PIN(6,  7), GPIO_OUT_LOW)                    /* A33 - PS2_CLK0 for USB_C0_DP_HPD */
GPIO(USB_C1_DP_HPD,          PIN(3,  7), GPIO_OUT_LOW)                    /* B20 - PS2_CLK2 for USB_C1_DP_HPD */
GPIO(CPU_PROCHOT,            PIN(D,  7), GPIO_OUT_LOW)                    /* A18 - GPIOD7 for EC_PCH_PROCHOT */
GPIO(ENABLE_TOUCHPAD,        PIN(A,  6), GPIO_OUT_LOW)                    /* B55 - PS2_CLK3/TA2/F_CS1# for TP_SHDN_L */
GPIO(BAT_PRESENT_L,          PIN(4,  5), GPIO_INPUT)                      /* B24 - ADC0 for EC_BATT_TMP */
GPIO(USB_PD_WAKE,            PIN(4,  3), GPIO_OUT_LOW)                    /* B23 - ADC2 for USB_PD_WAKE */
/* When asserted, ME does not lock security descriptor */
GPIO(PCH_SEC_DISABLE_L,      PIN(6,  3), GPIO_OUT_HIGH)                   /* B33 - PS2_DAT1 for FLASH_SECURITY_DISABLE_L */
GPIO(PCH_WAKE_L,             PIN(3,  5), GPIO_ODR_HIGH)                   /* A17 - GPIO35/TEST# for EC_PCH_WAKE_L */
GPIO(EC_FAN1_TTACH,          PIN(9,  3), GPIO_INPUT | GPIO_PULL_UP)       /* A46 - TA1/F_DIO2 for EC_FAN1_TACH */
/* Fan PWM output - NC / testing only */
GPIO(EC_FAN1_PWM,            PIN(C,  2), GPIO_OUT_LOW)                    /* A59 - PWM1 for EC_FAN1_PWM */
GPIO(PCH_ACOK,               PIN(7,  3), GPIO_OUT_LOW)                    /* B39 - PS2_CLK3/TA2for ROP_EC_ACOK */
/* Interrupts from accelerometer / gyro -- not yet implemented */
GPIO(ACCEL1_INT,             PIN(A,  3), GPIO_INPUT)                      /* A50 - SPIP_MOSI for ACCEL1_INT_L */
GPIO(ACCEL2_INT,             PIN(3,  3), GPIO_INPUT)                      /* B17 - GPIO33 for ACCEL2_INT_L */
GPIO(ACCEL3_INT,             PIN(8,  6), GPIO_INPUT)                      /* B46 - TXD/F_CS1# for ACCELGYRO3_INT_L */
GPIO(WLAN_OFF_L,             PIN(E,  0), GPIO_OUT_LOW)                    /* A24 - GPOE0 for WLAN_OFF_L */
/* RCIN# line to PCH for 8042 emulation */
GPIO(PCH_RCIN_L,             PIN(C,  5), GPIO_ODR_HIGH)                   /* A61 - KBRST# for EC_PCH_RCIN_L */
GPIO(USB2_OTG_VBUSSENSE,     PIN(D,  2), GPIO_OUT_LOW)                    /* B67 - GPIOD2 for USB2_OTG_VBUSSENSE */
GPIO(PCH_RSMRST_L,           PIN(8,  4), GPIO_OUT_LOW)                    /* B45 - GPIO84 for RSMRST_L */
/* prochot input from devices */
GPIO(PLATFORM_EC_PROCHOT,    PIN(3,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B19 - GPIO36 for PLATFORM_EC_PROCHOT */
GPIO(USB_C0_5V_EN,           PIN(0,  1), GPIO_OUT_LOW)                    /* B01 - GPIO01 for EN_USB_C0_5V_OUT */
GPIO(USB_C1_5V_EN,           PIN(E,  5), GPIO_OUT_LOW)                    /* B62 - JTAG_TMS1 for EN_USB_C1_5V_OUT */
GPIO(USB_C0_CHARGE_EN_L,     PIN(D,  3), GPIO_OUT_LOW)                    /* A64 - TB1 for EN_USB_C0_CHARGE_EC_L */
GPIO(PP1800_DX_SENSOR_EN,    PIN(0,  3), GPIO_OUT_LOW)                    /* B02 - KSO16 for EN_PP1800_DX_SENSOR */
/* From lid sensor */
GPIO(ENABLE_BACKLIGHT,       PIN(E,  2), GPIO_OUT_LOW)                    /* A41 - JTAG_TDI1 for EC_BL_DISABLE_L */
GPIO(PP3300_WLAN_EN,         PIN(E,  4), GPIO_OUT_LOW)                    /* A52 - GPIOE4 for EN_PP3300_DX_WLAN */
GPIO(BOARD_VERSION1,         PIN(0,  4), GPIO_INPUT)                      /* A02 - KSO13 for EC_BRD_ID1 */
GPIO(BOARD_VERSION2,         PIN(8,  2), GPIO_INPUT)                      /* B43 - KSO14 for EC_BRD_ID2 */
GPIO(BOARD_VERSION3,         PIN(8,  3), GPIO_INPUT)                      /* B44 - KSO15 for EC_BRD_ID3 */
GPIO(SYS_RESET_L,            PIN(E,  1), GPIO_ODR_HIGH)	                  /* B30 - GPIOE1 for SYS_RESET_L */

/*
 * TODO(crosbug.com/p/40848): These LEDs should be under control of the npcx
 * LED control unit. Remove these GPIO definitions once the LED control unit
 * is functional.
 */
GPIO(CHARGE_LED_1,           PIN(B,  7), GPIO_OUT_LOW)                    /* A57 - PWM5 for CHARGE_LED1 */
GPIO(CHARGE_LED_2,           PIN(C,  0), GPIO_OUT_LOW)                    /* B61 - PWM6 for CHARGE_LED2 */

/*
 * BATLOW_L and ROP_LDO_EN are stuffing options. Set as input to dynamically
 * handle the stuffing option based on board id. As both signals have external
 * pulls setting this pin as input won't harm anything.
 */
GPIO(BATLOW_L_PMIC_LDO_EN,   PIN(D,  5), GPIO_ODR_HIGH)                   /* A08 - JTAG_TCK1 for BATLOW_PLATFORM_RST */
GPIO(ACCEL4_INT,             PIN(A,  1), GPIO_INPUT)                      /* A49 - SPIP_SCLK for ACCELGYRO4_INT_L */
GPIO(PP1800_DX_AUDIO_EN,     PIN(8,  0), GPIO_OUT_LOW)                    /* A39 - PWM3 for EN_PP1800_DX_AUDIO */
/* NC / stuffing option */
GPIO(PCH_RTCRST,             PIN(C,  1), GPIO_INPUT | GPIO_PULL_UP)       /* A58 - GPIOC1 for EC_PCH_RTCRST */
GPIO(PMIC_SLP_SUS_L,         PIN(E,  3), GPIO_OUT_LOW)                    /* B51 - GPIOE3 for SLP_SUS_L_PMIC */
GPIO(USB_C1_CHARGE_EN_L,     PIN(C,  7), GPIO_OUT_LOW)                    /* A62 - GPIOC7 for EN_USB_C1_CHARGE_EC_L */

/* NC pins - Set as input and pull them up for better power consumption */
GPIO(NC_081,                 PIN(8,  1), GPIO_INPUT | GPIO_PULL_UP)       /* A40 - Unused pin */
GPIO(NC_0B6,                 PIN(B,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B60 - Unused pin */
GPIO(NC_066,                 PIN(6,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B35 - Unused pin */
GPIO(NC_0D6,                 PIN(D,  6), GPIO_INPUT | GPIO_PULL_UP)       /* B09 - Unused pin */

/* Alternate functions GPIO definitions */

/* UART pins */
#if NPCX_UART_MODULE2
ALTERNATE(PIN_MASK(6, 0x30), 1,  MODULE_UART,      0)   /* CR_SIN/SOUT for UART GPIO64/65 */
#else
ALTERNATE(PIN_MASK(1, 0x03), 1,  MODULE_UART,      0)   /* CR_SIN/SOUT for UART GPIO10/11 */
#endif

/* SPI pins */
ALTERNATE(PIN_MASK(A, 0x0A), 1,  MODULE_SPI,       0)   /* SPIP_MOSI/SPIP_SCLK for SPI GPIOA3/A1 */
ALTERNATE(PIN_MASK(9, 0x20), 1,  MODULE_SPI,       0)   /* SPIP_MISO for SPI GPIO95 */

/* I2C pins */
ALTERNATE(PIN_MASK(B, 0x0C), 1,  MODULE_I2C,       0)   /* I2C0SDA1/I2C0SCL1 for I2C GPIOB2/B3	   */
ALTERNATE(PIN_MASK(B, 0x30), 1,  MODULE_I2C,       0)   /* I2C0SDA0/I2C0SCL0 for I2C GPIOB4/B5	   */
ALTERNATE(PIN_MASK(8, 0x80), 1,  MODULE_I2C,       0)   /* I2C1SDA for I2C GPIO87 */
ALTERNATE(PIN_MASK(9, 0x07), 1,  MODULE_I2C,       0)   /* I2C1SCL/I2C2SDA/I2C2SCL for I2C GPIO90/91/92 */
ALTERNATE(PIN_MASK(D, 0x03), 1,  MODULE_I2C,       0)   /* I2C3SDA/I2C3SCL for I2C GPIOD0/D1    */

/* ADC pins */
ALTERNATE(PIN_MASK(4, 0x16), 1,  MODULE_ADC,       0)   /* ADC1/ADC3/ADC4 for ADC GPIO44/42/41 */

/* LED1 - GPIOB7. LED2 - GPIOC0 */
ALTERNATE(PIN_MASK(B, 0x80), 3,  MODULE_POWER_LED, 0)   /* PWM5 for CHARGE_LED1 */
ALTERNATE(PIN_MASK(C, 0x01), 3,  MODULE_POWER_LED, 0)   /* PWM6 for CHARGE_LED2 */

