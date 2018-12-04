/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* USB-C interrupts */
GPIO_INT(USB_C0_PD_INT_ODL,    PIN(A, 0), GPIO_INT_FALLING, tcpc_alert_event)  /* Interrupt from port-0 TCPC */
GPIO_INT(USB_C1_PD_INT_ODL,    PIN(F, 5), GPIO_INT_FALLING, tcpc_alert_event)  /* Interrupt from port-1 TCPC */
GPIO_INT(USB_C0_SWCTL_INT_ODL, PIN(0, 3), GPIO_INT_FALLING, ppc_interrupt)  /* Interrupt from port-0 PPC */
GPIO_INT(USB_C0_BC12_INT_L,    PIN(6, 1), GPIO_INT_FALLING, usb0_evt)  /* Interrupt from port-0 BC1.2 */
GPIO_INT(USB_C1_BC12_INT_L,    PIN(8, 2), GPIO_INT_FALLING, usb1_evt)  /* Interrupt from port-1 BC1.2 */
GPIO_INT(USB_C0_VBUS_DET_L,    PIN(6, 2), GPIO_INT_BOTH | GPIO_PULL_UP, vbus0_evt)  /* BC1.2 VBUS detection on port-0 */
GPIO_INT(USB_C1_VBUS_DET_L,    PIN(8, 3), GPIO_INT_BOTH | GPIO_PULL_UP, vbus1_evt)  /* BC1.2 VBUS detection on port-1 */
GPIO_INT(USB_C0_CABLE_DET,     PIN(3, 7), GPIO_INT_RISING, anx74xx_cable_det_interrupt)  /* Cable detection from port-0 TCPC */
GPIO_INT(USB_C1_OC_ODL,        PIN(7, 2), GPIO_INT_FALLING, usb1_oc_evt)  /* Port-1 power switch over-current */
GPIO_INT(ACCEL_GYRO_INT_L,     PIN(D, 3), GPIO_INT_FALLING | GPIO_SEL_1P8V, bmi160_interrupt)  /* Accelerometer/gyro interrupt */

/* System interrupts */
GPIO_INT(AC_PRESENT,        PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt) /* ACOK_OD */
GPIO_INT(POWER_BUTTON_L,    PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)  /* EC_PWR_BTN_ODL */
GPIO_INT(VOLUME_DOWN_L,     PIN(7, 0), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)  /* EC_VOLDN_BTN_ODL */
GPIO_INT(VOLUME_UP_L,       PIN(F, 2), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)  /* EC_VOLUP_BTN_ODL */
GPIO_INT(WP_L,              PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)  /* EC_WP_ODL */
GPIO_INT(LID_OPEN,          PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)  /* LID_OPEN_EC */
GPIO_INT(AP_RST_REQ,        PIN(C, 2), GPIO_INT_RISING | GPIO_PULL_DOWN | GPIO_SEL_1P8V, chipset_reset_request_interrupt)  /* Reset request from AP */
GPIO_INT(AP_RST_L,          PIN(C, 1), GPIO_INT_BOTH | GPIO_SEL_1P8V, power_signal_interrupt)
GPIO_INT(PS_HOLD,           PIN(D, 4), GPIO_INT_BOTH | GPIO_PULL_DOWN | GPIO_SEL_1P8V, power_signal_interrupt)  /* Indicate when AP triggers reset/shutdown */
GPIO_INT(PMIC_FAULT_L,      PIN(7, 6), GPIO_INT_BOTH | GPIO_SEL_1P8V, power_signal_interrupt)  /* Any PMIC fault? */
/*
 * When switch-cap is off, the POWER_GOOD signal is floating. Need a pull-down
 * to make it low. Overload the interrupt function chipset_warm_reset_interrupt
 * for not only signalling power_signal_interrupt but also handling the logic
 * of WARM_RESET_L which is pulled-up by the same rail of POWER_GOOD.
 */
GPIO_INT(POWER_GOOD,        PIN(5, 4), GPIO_INT_BOTH | GPIO_PULL_DOWN, chipset_warm_reset_interrupt)  /* SRC_PP1800_S4A from PMIC */
GPIO_INT(WARM_RESET_L,      PIN(F, 4), GPIO_INT_BOTH | GPIO_SEL_1P8V, chipset_warm_reset_interrupt)  /* AP warm reset */
GPIO_INT(SHI_CS_L,          PIN(5, 3), GPIO_INT_FALLING | GPIO_PULL_DOWN, shi_cs_event)  /* AP_EC_SPI_CS_L */
GPIO_INT(CC_LID_BASE_ADC,   PIN(4, 5), GPIO_INT_BOTH, base_detect_interrupt)  /* Base detection */

/*
 * EC_RST_ODL acts as a wake source from PSL hibernate mode. However, it does
 * not need to be an interrupt for normal EC operations. Thus, configure it as
 * GPIO_INT_BOTH with wake on low-to-high edge using GPIO_HIB_WAKE_HIGH so that
 * PSL common code can configure PSL_IN correctly.
 *
 * Use the rising edge to wake EC up. If we chose the falling edge, it would
 * still wake EC up, but EC is in an intermediate state until the signal goes
 * back to high.
 */
GPIO(EC_RST_ODL,            PIN(0, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH)  /* Wake source: EC reset */
GPIO(ENTERING_RW,           PIN(E, 1), GPIO_OUT_LOW)   /* EC_ENTERING_RW: Indicate when EC is entering RW code */
GPIO(CCD_MODE_ODL,          PIN(E, 3), GPIO_INPUT)     /* Case Closed Debug Mode */
GPIO(BATT_PRES_ODL,         PIN(E, 5), GPIO_INPUT)     /* EC_BATT_PRES_ODL: Battery Present */
GPIO(PROCHOT_L,             PIN(3, 4), GPIO_INPUT)

/* PMIC/AP 1.8V */
GPIO(PM845_RESIN_L,         PIN(3, 2), GPIO_ODR_HIGH | GPIO_SEL_1P8V)  /* PMIC reset trigger */
GPIO(PMIC_KPD_PWR_ODL,      PIN(D, 6), GPIO_ODR_HIGH | GPIO_SEL_1P8V)  /* PMIC power button */
GPIO(EC_INT_L,              PIN(A, 2), GPIO_ODR_HIGH)  /* Interrupt line between AP and EC */
GPIO(AP_SUSPEND_L,          PIN(5, 7), GPIO_INPUT)     /* Suspend signal from AP */

/* Power enables */
GPIO(SWITCHCAP_ON_L,        PIN(D, 5), GPIO_OUT_LOW)   /* Enable switch cap. XXX: It's active-high */
GPIO(VBOB_EN,               PIN(9, 5), GPIO_OUT_LOW)   /* Enable VBOB */
GPIO(EN_PP3300_A,           PIN(A, 6), GPIO_OUT_LOW)   /* Enable PP3300 */
GPIO(EN_PP5000,             PIN(6, 7), GPIO_OUT_LOW)   /* EN_PP5000_A: Enable PP5000 */
GPIO(ENABLE_BACKLIGHT,      PIN(B, 6), GPIO_OUT_LOW)   /* EC_BL_DISABLE_L: Backlight disable signal from EC */

/* Sensors */
GPIO(ALS_INT_L,             PIN(5, 0), GPIO_INPUT)     /* ALS sensor interrupt */
GPIO(P_SENSOR_INT_L,        PIN(F, 3), GPIO_INPUT | GPIO_SEL_1P8V) /* P-sensor interrupt */
GPIO(RCAM_VSYNC,            PIN(4, 0), GPIO_INPUT | GPIO_SEL_1P8V) /* VSYNC from rear camera */

/* Base */
GPIO(EN_PPVAR_VAR_BASE,     PIN(0, 4), GPIO_OUT_LOW)   /* Power to the base */
GPIO(EN_CC_LID_BASE_PH,     PIN(D, 1), GPIO_ODR_HIGH)
GPIO(EN_CC_LID_BASE_PULLDN, PIN(D, 0), GPIO_ODR_HIGH)
GPIO(REVERSE_DOCK_EC,       PIN(C, 6), GPIO_INPUT)     /* Indicate if the dock is reversed */
GPIO(CC_LID_RX_BASE_TX,     PIN(7, 5), GPIO_INPUT)
GPIO(CC_LID_RX_BASE_RX,     PIN(8, 6), GPIO_INPUT)

/* LEDs */
GPIO(CHG_LED_Y_C0,          PIN(C, 3), GPIO_OUT_LOW)   /* EC_CHG_LED_Y_C0 */
GPIO(CHG_LED_W_C0,          PIN(C, 4), GPIO_OUT_LOW)   /* EC_CHG_LED_W_C0 */
GPIO(CHG_LED_Y_C1,          PIN(6, 0), GPIO_OUT_LOW)   /* EC_CHG_LED_Y_C1 */
GPIO(CHG_LED_W_C1,          PIN(C, 0), GPIO_OUT_LOW)   /* EC_CHG_LED_W_C1 */

/*
 * USB HS muxes
 *
 *               C0_MUX:D ----- C0_BC12 ---- C0_PORT
 * AP --+------- C0_MUX:D1
 *      |   +--- C0_MUX:D2
 *      |   |
 *      |   |
 *      |   |    C1_MUX:D ----- C1_BC12 ---- C1_PORT
 *      |   | +- C1_MUX:D1
 *      +------- C1_MUX:D2
 *          | |
 * AP --- USB_HUB
 */
/* Switch both port-0 and port-1 to the hub, which matches the SS path. */
GPIO(USB_C0_HS_MUX_OE_L,    PIN(A, 4), GPIO_OUT_LOW)
GPIO(USB_C0_HS_MUX_SEL,     PIN(A, 3), GPIO_OUT_HIGH)  /* L:D1(AP), H:D2(hub) */
GPIO(USB_C1_HS_MUX_OE_L,    PIN(7, 3), GPIO_OUT_LOW)
GPIO(USB_C1_HS_MUX_SEL,     PIN(7, 4), GPIO_OUT_LOW)   /* L:D1(hub), H:D2(AP) */

/* USB-C port-0 controls */
GPIO(USB_C0_PD_RST_R_L,     PIN(F, 1), GPIO_OUT_HIGH)  /* Port-0 TCPC chip reset */
GPIO(EN_USB_C0_TCPC_PWR,    PIN(C, 5), GPIO_OUT_LOW)   /* Port-0 TCPC power enable */

/* USB-C port-1 controls */
GPIO(USB_C1_PD_RST_ODL,     PIN(E, 4), GPIO_ODR_HIGH)  /* Port-1 TCPC chip reset */
GPIO(EN_USB_C1_5V_OUT,      PIN(6, 3), GPIO_OUT_LOW)   /* Port-1 power switch 5V output */
GPIO(EN_USB_C1_3A,          PIN(5, 6), GPIO_OUT_LOW)   /* Port-1 power switch 3A current */
GPIO(EN_USB_C1_CHARGE_EC_L, PIN(B, 1), GPIO_OUT_LOW)   /* Port-1 enable charging */
GPIO(USBC_MUX_CONF1,        PIN(5, 1), GPIO_OUT_HIGH)  /* Port-1 enable DP switch */

/* USB-C port-1 interrupts */
GPIO(USB_C1_DP_HPD,         PIN(9, 6), GPIO_INPUT)     /* DP HPD from port-1 TCPC */

/* I2C */
GPIO(I2C0_SCL,              PIN(B, 5), GPIO_INPUT)     /* EC_I2C_POWER_SCL */
GPIO(I2C0_SDA,              PIN(B, 4), GPIO_INPUT)     /* EC_I2C_POWER_SDA */
GPIO(I2C1_SCL,              PIN(9, 0), GPIO_INPUT)     /* EC_I2C_USB_C0_PD_SCL */
GPIO(I2C1_SDA,              PIN(8, 7), GPIO_INPUT)     /* EC_I2C_USB_C0_PD_SDA */
GPIO(I2C2_SCL,              PIN(9, 2), GPIO_INPUT)     /* EC_I2C_USB_C1_PD_SCL */
GPIO(I2C2_SDA,              PIN(9, 1), GPIO_INPUT)     /* EC_I2C_USB_C1_PD_SDA */
GPIO(I2C5_SCL,              PIN(3, 3), GPIO_INPUT)     /* EC_I2C_EEPROM_SCL */
GPIO(I2C5_SDA,              PIN(3, 6), GPIO_INPUT)     /* EC_I2C_EEPROM_SDA */
GPIO(I2C7_SCL,              PIN(B, 3), GPIO_INPUT |
                                       GPIO_SEL_1P8V)  /* EC_I2C_SENSOR_SCL */
GPIO(I2C7_SDA,              PIN(B, 2), GPIO_INPUT |
                                       GPIO_SEL_1P8V)  /* EC_I2C_SENSOR_SDA */

/* Board/SKU IDs */
GPIO(BOARD_VERSION1,        PIN(C, 7), GPIO_INPUT)     /* BRD_ID1 */
GPIO(BOARD_VERSION2,        PIN(9, 3), GPIO_INPUT)     /* BRD_ID2 */
GPIO(BOARD_VERSION3,        PIN(8, 0), GPIO_INPUT)     /* BRD_ID3 */
GPIO(SKU_ID1,               PIN(F, 0), GPIO_INPUT)
GPIO(SKU_ID2,               PIN(4, 1), GPIO_INPUT)

/* Switchcap */
/*
 * GPIO0 is configured as PVC_PG. When the chip in power down mode, it outputs
 * high-Z. Set pull-down to avoid floating.
 */
GPIO(DA9313_GPIO0,          PIN(E, 2), GPIO_INPUT | GPIO_PULL_DOWN) /* Switchcap GPIO0 */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)            /* UART (GPIO64/65) */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)             /* I2C0 (GPIOB4/B5) */
ALTERNATE(PIN_MASK(9, 0x07), 1, MODULE_I2C, 0)             /* I2C1 SDA (GPIO90), I2C2 (GPIO91/92) */
ALTERNATE(PIN_MASK(8, 0x80), 1, MODULE_I2C, 0)             /* I2C1 SCL (GPIO87) */
ALTERNATE(PIN_MASK(3, 0x48), 1, MODULE_I2C, 0)             /* I2C5 (GPIO33/36) */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, GPIO_SEL_1P8V) /* I2C7 (GPIOB2/B3) - 1.8V */
ALTERNATE(PIN_MASK(4, 0x1C), 0, MODULE_ADC, 0)             /* ADC1 (GPIO44), ADC2 (GPIO43), ADC3 (GPIO42) */
ALTERNATE(PIN_MASK(4, 0xC0), 1, MODULE_SPI, GPIO_SEL_1P8V) /* SHI_SDO (GPIO47), SHI_SDI (GPIO46) */
ALTERNATE(PIN_MASK(5, 0x28), 1, MODULE_SPI, GPIO_SEL_1P8V) /* SHI_SCLK (GPIO55), SHI_CS# (GPIO53) */
ALTERNATE(PIN_MASK(B, 0x80), 1, MODULE_PWM, 0)             /* PWM5 (GPIOB7) */
ALTERNATE(PIN_MASK(D, 0x04), 1, MODULE_PMU, 0)             /* PSL_IN1 (GPIOD2) - LID_OPEN_EC */
ALTERNATE(PIN_MASK(0, 0x01), 1, MODULE_PMU, 0)             /* PSL_IN2 (GPIO00) - ACOK_OD */
ALTERNATE(PIN_MASK(0, 0x02), 1, MODULE_PMU, 0)             /* PSL_IN3 (GPIO01) - EC_PWR_BTN_ODL */
ALTERNATE(PIN_MASK(0, 0x04), 1, MODULE_PMU, 0)             /* PSL_IN4 (GPIO02) - EC_RST_ODL */
