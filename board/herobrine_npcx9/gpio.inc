/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* USB interrupts */
GPIO_INT(USB_C0_PD_INT_ODL,    PIN(E, 0), GPIO_INT_FALLING, tcpc_alert_event)  /* Interrupt from port-0 TCPC */
GPIO_INT(USB_C1_PD_INT_ODL,    PIN(F, 5), GPIO_INT_FALLING, tcpc_alert_event)  /* Interrupt from port-1 TCPC */
GPIO_INT(USB_C0_SWCTL_INT_ODL, PIN(0, 3), GPIO_INT_FALLING, ppc_interrupt)  /* Interrupt from port-0 PPC */
GPIO_INT(USB_C1_SWCTL_INT_ODL, PIN(4, 0), GPIO_INT_FALLING, ppc_interrupt)  /* Interrupt from port-1 PPC */
GPIO_INT(USB_C0_BC12_INT_L,    PIN(6, 1), GPIO_INT_FALLING | GPIO_PULL_UP, usb0_evt)  /* Interrupt from port-0 BC1.2 */
GPIO_INT(USB_C1_BC12_INT_L,    PIN(8, 2), GPIO_INT_FALLING | GPIO_PULL_UP, usb1_evt)  /* Interrupt from port-1 BC1.2 */
GPIO_INT(USB_A0_OC_ODL,        PIN(F, 4), GPIO_INT_BOTH | GPIO_PULL_UP, usba_oc_interrupt)

/* System interrupts */
GPIO_INT(CHG_ACOK_OD,       PIN(D, 2), GPIO_INT_BOTH, extpower_interrupt)  /* ACOK */
GPIO_INT(EC_PWR_BTN_ODL,    PIN(0, 0), GPIO_INT_BOTH, power_button_interrupt)  /* Power button */
GPIO_INT(EC_VOLDN_BTN_ODL,  PIN(6, 2), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)  /* Volume Up button */
GPIO_INT(EC_VOLUP_BTN_ODL,  PIN(C, 2), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)  /* Volume Down button */
GPIO_INT(EC_WP_ODL,         PIN(D, 3), GPIO_INT_BOTH, switch_interrupt)  /* Write protection */
GPIO_INT(LID_OPEN_EC,       PIN(0, 1), GPIO_INT_BOTH, lid_interrupt)  /* Lid open */
GPIO_INT(AP_RST_L,          PIN(5, 1), GPIO_INT_BOTH, chipset_ap_rst_interrupt)  /* PMIC to signal AP reset */
GPIO_INT(PS_HOLD,           PIN(A, 6), GPIO_INT_BOTH | GPIO_PULL_DOWN, power_signal_interrupt)  /* Indicate when AP triggers reset/shutdown */
GPIO_INT(AP_SUSPEND,        PIN(5, 7), GPIO_INT_BOTH, power_signal_interrupt)  /* Suspend signal from PMIC */
/*
 * When switch-cap is off, the POWER_GOOD signal is floating. Need a pull-down
 * to make it low.
 */
GPIO_INT(MB_POWER_GOOD,     PIN(3, 7), GPIO_INT_BOTH | GPIO_PULL_DOWN, power_signal_interrupt)  /* PP1800_L18B from PMIC */
GPIO_INT(WARM_RESET_L,      PIN(B, 0), GPIO_INT_BOTH, power_signal_interrupt)  /* AP warm reset */
GPIO_INT(AP_EC_SPI_CS_L,    PIN(5, 3), GPIO_INT_FALLING | GPIO_PULL_DOWN, shi_cs_event)  /* EC SPI Chip Select */

/* Sensor interrupts */
GPIO_INT(TABLET_MODE_L,     PIN(C, 6), GPIO_INT_BOTH, gmr_tablet_switch_isr)
GPIO_INT(ACCEL_GYRO_INT_L,  PIN(A, 3), GPIO_INT_FALLING, bmi260_interrupt)  /* Accelerometer/gyro interrupt */

GPIO(RTC_EC_WAKE_ODL ,      PIN(0, 2), GPIO_INPUT)     /* RTC interrupt */
GPIO(EC_ENTERING_RW,        PIN(7, 2), GPIO_OUT_LOW)   /* Indicate when EC is entering RW code */
GPIO(CCD_MODE_ODL,          PIN(6, 3), GPIO_INPUT)     /* Case Closed Debug Mode */
GPIO(EC_BATT_PRES_ODL,      PIN(E, 5), GPIO_INPUT)     /* Battery Present */
GPIO(EC_GSC_PACKET_MODE,    PIN(8, 3), GPIO_OUT_LOW)   /* GSC Packet Mode */

/* PMIC/AP 1.8V */
GPIO(PMIC_RESIN_L,          PIN(A, 0), GPIO_ODR_HIGH)  /* PMIC reset trigger */
GPIO(PMIC_KPD_PWR_ODL,      PIN(A, 2), GPIO_ODR_HIGH)  /* PMIC power button */
GPIO(AP_EC_INT_L,           PIN(5, 6), GPIO_ODR_HIGH)  /* Interrupt line between AP and EC */

/* Power enables */
GPIO(SWITCHCAP_ON,          PIN(D, 5), GPIO_OUT_LOW)   /* Enable switch cap */
GPIO(EN_PP5000_S3,          PIN(7, 3), GPIO_OUT_LOW)   /* Enable PP5000 */
GPIO(EC_BL_DISABLE_L,       PIN(B, 6), GPIO_OUT_LOW)   /* Backlight disable signal from EC */

/* Sensors */
GPIO(LID_ACCEL_INT_L,       PIN(A, 1), GPIO_INPUT)     /* Lid accel sensor interrupt */
/* Control the gate for trackpad IRQ. High closes the gate.
 * This is always set low so that the OS can manage the trackpad. */
GPIO(TP_INT_GATE,           PIN(7, 4), GPIO_OUT_LOW)

/* USB-C */
GPIO(USB_C0_PD_RST_L,       PIN(F, 1), GPIO_OUT_HIGH)  /* Port-0 TCPC chip reset */
GPIO(USB_C1_PD_RST_L,       PIN(E, 4), GPIO_OUT_HIGH)  /* Port-1 TCPC chip reset */
GPIO(DP_MUX_OE_L,           PIN(B, 1), GPIO_ODR_HIGH)  /* DP mux enable, actually Open-Drain */
GPIO(DP_MUX_SEL,            PIN(4, 5), GPIO_OUT_LOW)   /* DP mux selection: L:C0, H:C1 */
GPIO(DP_HOT_PLUG_DET_R,     PIN(9, 5), GPIO_OUT_LOW)   /* DP HPD to AP */

/* USB-A */
GPIO(EN_USB_A_5V,           PIN(F, 0), GPIO_OUT_LOW)
GPIO(USB_A_CDP_ILIM_EN_L,   PIN(7, 5), GPIO_OUT_HIGH)  /* H:CDP, L:SDP. Only one USB-A port, always CDP */

/* LEDs */
GPIO(EC_CHG_LED_Y_C0,       PIN(6, 0), GPIO_OUT_LOW)
GPIO(EC_CHG_LED_W_C0,       PIN(C, 0), GPIO_OUT_LOW)
GPIO(EC_CHG_LED_Y_C1,       PIN(C, 3), GPIO_OUT_LOW)
GPIO(EC_CHG_LED_W_C1,       PIN(C, 4), GPIO_OUT_LOW)

/*
 * SPI host interface - enable PDs by default. These will be made functional
 * by the SHI driver when the AP powers up, and restored back to GPIO when
 * the AP powers down.
 */
GPIO(AP_EC_SPI_MOSI,        PIN(4, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(AP_EC_SPI_MISO,        PIN(4, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(AP_EC_SPI_CLK,         PIN(5, 5), GPIO_INPUT | GPIO_PULL_DOWN)

/* I2C */
GPIO(EC_I2C_POWER_SCL,      PIN(B, 5), GPIO_INPUT)
GPIO(EC_I2C_POWER_SDA,      PIN(B, 4), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_PD_SCL,  PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_PD_SDA,  PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_PD_SCL,  PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_PD_SDA,  PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C_RTC_SCL,        PIN(F, 3), GPIO_INPUT)
GPIO(EC_I2C_RTC_SDA,        PIN(F, 2), GPIO_INPUT)
GPIO(EC_I2C_EEPROM_SCL,     PIN(3, 3), GPIO_INPUT)
GPIO(EC_I2C_EEPROM_SDA,     PIN(3, 6), GPIO_INPUT)
GPIO(EC_I2C_SENSOR_SCL,     PIN(B, 3), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_SENSOR_SDA,     PIN(B, 2), GPIO_INPUT | GPIO_SEL_1P8V)

/* Board/SKU IDs */
GPIO(BRD_ID0,               PIN(9, 4), GPIO_INPUT)
GPIO(BRD_ID1,               PIN(9, 7), GPIO_INPUT)
GPIO(BRD_ID2,               PIN(A, 5), GPIO_INPUT)
GPIO(SKU_ID0,               PIN(6, 7), GPIO_INPUT)
GPIO(SKU_ID1,               PIN(7, 0), GPIO_INPUT)
GPIO(SKU_ID2,               PIN(E, 1), GPIO_INPUT)

/* Switchcap */
/*
 * GPIO0 is configured as PVC_PG. When the chip in power down mode, it outputs
 * high-Z. Set pull-down to avoid floating.
 */
GPIO(SRC_VPH_PWR_PG,        PIN(E, 2), GPIO_INPUT | GPIO_PULL_DOWN) /* Switchcap GPIO0 */

/* Special straps */
GPIO(ARM_X86,               PIN(6, 6), GPIO_OUT_LOW)  /* NC, low for power saving */

/* Unused GPIOs, NC. */
UNUSED(PIN(5, 2))
UNUSED(PIN(5, 4))
UNUSED(PIN(7, 6))
UNUSED(PIN(C, 5))
UNUSED(PIN(D, 1))
UNUSED(PIN(D, 0))
UNUSED(PIN(E, 3))
UNUSED(PIN(C, 1))
UNUSED(PIN(0, 4))
UNUSED(PIN(D, 6))
UNUSED(PIN(3, 2))
UNUSED(PIN(3, 5))
UNUSED(PIN(D, 7))
UNUSED(PIN(8, 6))
UNUSED(PIN(D, 4))
UNUSED(PIN(4, 1))
UNUSED(PIN(3, 4))
UNUSED(PIN(C, 7))
UNUSED(PIN(A, 4))
UNUSED(PIN(9, 6))
UNUSED(PIN(9, 3))
UNUSED(PIN(A, 7))
UNUSED(PIN(5, 0))
UNUSED(PIN(8, 1))

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)            /* UART (GPIO64/65) */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)             /* I2C0 (GPIOB4/B5) */
ALTERNATE(PIN_MASK(9, 0x07), 1, MODULE_I2C, 0)             /* I2C1 SDA (GPIO90), I2C2 (GPIO91/92) */
ALTERNATE(PIN_MASK(8, 0x80), 1, MODULE_I2C, 0)             /* I2C1 SCL (GPIO87) */
ALTERNATE(PIN_MASK(F, 0x0C), 1, MODULE_I2C, 0)             /* I2C4 (GPIOF2/F3) */
ALTERNATE(PIN_MASK(3, 0x48), 1, MODULE_I2C, 0)             /* I2C5 (GPIO33/36) */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, GPIO_SEL_1P8V) /* I2C7 (GPIOB2/B3) - 1.8V */
ALTERNATE(PIN_MASK(4, 0x1C), 0, MODULE_ADC, 0)             /* ADC1 (GPIO44), ADC2 (GPIO43), ADC3 (GPIO42) */
ALTERNATE(PIN_MASK(4, 0xC0), 1, MODULE_SPI, GPIO_SEL_1P8V) /* SHI_SDO (GPIO47), SHI_SDI (GPIO46) */
ALTERNATE(PIN_MASK(5, 0x28), 1, MODULE_SPI, GPIO_SEL_1P8V) /* SHI_SCLK (GPIO55), SHI_CS# (GPIO53) */
ALTERNATE(PIN_MASK(8, 0x01), 0, MODULE_PWM, 0)             /* PWM3 (GPIO80) - KB_BL_PWM */
ALTERNATE(PIN_MASK(B, 0x80), 1, MODULE_PWM, 0)             /* PWM5 (GPIOB7) - EDP_BKLTCTL */

/* Keyboard */
#define GPIO_KB_INPUT           (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT          (GPIO_ODR_HIGH)
#define GPIO_KB_OUTPUT_COL2     (GPIO_OUT_LOW)

/* Keyboard alternate functions */
ALTERNATE(PIN_MASK(0, 0xE0), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)  /* KSO10 (GPIO07), KSO11 (GPIO06), KSO12 (GPIO05) */
ALTERNATE(PIN_MASK(1, 0x7F), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)  /* KSO03 (GPIO16), KSO04 (GPIO15), KSO05 (GPIO14), KSO06 (GPIO13), KSO07 (GPIO12), KSO08 (GPIO11), KSO09 (GPIO10) */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)  /* KSO00 (GPIO21), KSO01 (GPIO20) */
ALTERNATE(PIN_MASK(2, 0xFC), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)   /* KSI2 (GPIO27), KSI3 (GPIO26), KSI4 (GPIO25), KSI5 (GPIO24), KSI6 (GPIO23), KSI7 (GPIO22) */
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)   /* KSI0 (GPIO31), KSI1 (GPIO30) */
GPIO(EC_KSO_02_INV,       PIN(1, 7), GPIO_KB_OUTPUT_COL2)              /* KSO02 (GPIO17) */
