/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USB_C0_PD_INT_ODL,  PIN(3, 7), GPIO_INT_FALLING,                tcpc_alert_event)
GPIO_INT(USB_C1_PD_INT_ODL,  PIN(C, 5), GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)
#ifdef CONFIG_POWER_S0IX
GPIO_INT(SLP_S0_L,           PIN(7, 5), GPIO_INT_BOTH,                    power_signal_interrupt)
#endif
GPIO_INT(SLP_SUS_L_PCH,      PIN(6, 2), GPIO_INT_BOTH,                    power_signal_interrupt)
GPIO_INT(ROP_EC_RSMRST_L,    PIN(B, 0), GPIO_INT_BOTH,                    power_signal_interrupt)
GPIO_INT(ROP_DSW_PWROK,      PIN(C, 7), GPIO_INT_BOTH,                    power_signal_interrupt)
GPIO_INT(PWR_BTN_ODL,        PIN(0, 4), GPIO_INT_BOTH | GPIO_PULL_UP,     power_button_interrupt)
GPIO_INT(LID_OPEN,           PIN(6, 7), GPIO_INT_BOTH,                    lid_interrupt)
GPIO_INT(VOLDN_BTN,          PIN(8, 3), GPIO_INT_BOTH,                    button_interrupt)
GPIO_INT(VOLUP_BTN,          PIN(3, 6), GPIO_INT_BOTH,                    button_interrupt)
GPIO_INT(WP_L,               PIN(4, 0), GPIO_INT_BOTH,                    switch_interrupt)
GPIO_INT(ROP_EC_ACOK,        PIN(C, 1), GPIO_INT_BOTH,                    extpower_interrupt)
GPIO_INT(USB_C0_VBUS_DET_L,  PIN(9, 3), GPIO_INT_BOTH | GPIO_PULL_UP,     vbus0_evt)
GPIO_INT(USB_C1_VBUS_DET_L,  PIN(9, 7), GPIO_INT_BOTH | GPIO_PULL_UP,     vbus1_evt)
GPIO_INT(USB_C0_BC12_INT_L,  PIN(D, 3), GPIO_INT_FALLING,                 usb0_evt)
GPIO_INT(USB_C1_BC12_INT_L,  PIN(3, 3), GPIO_INT_FALLING,                 usb1_evt)
GPIO_INT(BASE_SIXAXIS_INT_L, PIN(7, 3), GPIO_INT_FALLING | GPIO_SEL_1P8V, bmi160_interrupt)
GPIO_INT(TABLET_MODE,        PIN(C, 6), GPIO_INT_BOTH,                    gmr_tablet_switch_isr)

GPIO(EN_PP3300_TRACKPAD,     PIN(4, 5), GPIO_OUT_LOW)                /* Enable TouchPad */
GPIO(PCH_RTCRST,             PIN(8, 2), GPIO_OUT_LOW)                /* RTCRST# to SOC */
GPIO(BL_DISABLE_L,           PIN(5, 6), GPIO_OUT_LOW)                /* Enable Backlight */
GPIO(WLAN_OFF_L,             PIN(7, 2), GPIO_OUT_LOW)                /* Disable WLAN */
GPIO(EN_PP3300_DX_WLAN,      PIN(A, 7), GPIO_OUT_LOW)                /* Enable WLAN 3.3V Power */
GPIO(PCH_PROCHOT,            PIN(8, 1), GPIO_OUT_HIGH)               /* PROCHOT# to SOC */
GPIO(PCH_ACPRESENT,          PIN(5, 0), GPIO_ODR_LOW)                /* ACOK to SOC */
GPIO(PCH_WAKE_L,             PIN(A, 3), GPIO_ODR_HIGH)               /* Wake SOC */
GPIO(PCH_RSMRST_L,           PIN(7, 0), GPIO_OUT_LOW)                /* RSMRST# to SOC */
GPIO(PCH_PWR_BTN_L,          PIN(C, 4), GPIO_ODR_HIGH)               /* Power Button to SOC */
GPIO(PLATFORM_RST,           PIN(A, 6), GPIO_OUT_LOW)                /* EC Reset to LDO_EN */
GPIO(SYS_RESET_L,            PIN(6, 1), GPIO_ODR_HIGH)               /* Cold Reset to SOC */
GPIO(SLP_SUS_L_PMIC,         PIN(8, 5), GPIO_OUT_LOW)                /* SLP_SUS# to PMIC */
GPIO(BATT_PRES_L,            PIN(3, 4), GPIO_INPUT)                  /* Battery Present */
GPIO(CCD_MODE_ODL,           PIN(6, 3), GPIO_INPUT)                  /* Case Closed Debug Mode */
GPIO(ENTERING_RW,            PIN(7, 6), GPIO_OUTPUT)                 /* EC Entering RW */
GPIO(ROP_EC_INT_L,           PIN(6, 0), GPIO_INPUT)                  /* PMIC interrupt */
GPIO(KB_BL_EN,               PIN(7, 1), GPIO_OUT_LOW)                /* Keyboard backlight enable */
#ifndef CONFIG_POWER_S0IX
GPIO(SLP_S0_L,               PIN(7, 5), GPIO_INPUT)
#endif

/* Sensor Power */
GPIO(EN_PP1800_DX_SENSOR,    PIN(E, 7), GPIO_OUT_LOW)

/* Reserved, no function */
GPIO(EN_PP3300_USB_PD,       PIN(0, 1), GPIO_INPUT)
/* Reserved, output low to disable Board ID ADC by default */
GPIO(BRD_ID_EN,              PIN(0, 2), GPIO_OUT_LOW)                /* Enable for board ID ADC */
/* Reserved, for the lid accelerator interrupt */
GPIO(LID_ACCEL_INT_L,        PIN(D, 2), GPIO_INPUT | GPIO_SEL_1P8V | GPIO_PULL_UP)  /* LID Accelerator interrupt */
/* Reserved, changing touchpad interrupt behavior for wakeup */
GPIO(TP_INT_CONN,            PIN(C, 2), GPIO_INPUT)                  /* Touchpad interrupt */
/* Reserved, output low to enable touchpad interrupt by default */
GPIO(TP_INT_EN,              PIN(A, 1), GPIO_OUT_LOW)                /* Enable Touchpad interrupt */

/* I2C pins - these will be reconfigured for alternate function below */
GPIO(I2C0_0_SCL,             PIN(B, 5), GPIO_INPUT)                  /* EC_I2C0_0_USBC_3V3_SCL */
GPIO(I2C0_0_SDA,             PIN(B, 4), GPIO_INPUT)                  /* EC_I2C0_0_USBC_3V3_SDA */
GPIO(I2C0_1_SCL,             PIN(B, 3), GPIO_INPUT)                  /* EC_I2C0_1_USBC_3V3_SCL */
GPIO(I2C0_1_SDA,             PIN(B, 2), GPIO_INPUT)                  /* EC_I2C0_1_USBC_3V3_SDA */
GPIO(I2C1_SCL,               PIN(9, 0), GPIO_INPUT)                  /* EC_I2C1_3V3_SCL */
GPIO(I2C1_SDA,               PIN(8, 7), GPIO_INPUT)                  /* EC_I2C1_3V3_SDA */
GPIO(I2C2_SCL,               PIN(9, 2), GPIO_INPUT)                  /* EC_I2C2_PMIC_3V3_SCL */
GPIO(I2C2_SDA,               PIN(9, 1), GPIO_INPUT)                  /* EC_I2C2_PMIC_3V3_SDA */
GPIO(I2C3_SCL,               PIN(D, 1), GPIO_INPUT | GPIO_SEL_1P8V)  /* EC_I2C3_SENSOR_1V8_SCL */
GPIO(I2C3_SDA,               PIN(D, 0), GPIO_INPUT | GPIO_SEL_1P8V)  /* EC_I2C3_SENSOR_1V8_SDA */

/* rev0: 5V enables: INPUT=1.5A, OUT_LOW=OFF, OUT_HIGH=3A */
GPIO(EN_USB_C0_5V_OUT,       PIN(3, 2), GPIO_OUT_LOW)                /* C0 5V Enable */
GPIO(EN_USB_C0_3A,           PIN(6, 6), GPIO_OUT_LOW)                /* C0 Enable 3A */
GPIO(EN_USB_C0_CHARGE_EC_L,  PIN(C, 0), GPIO_OUT_LOW)                /* C0 Charge enable */
GPIO(EN_USB_C1_5V_OUT,       PIN(B, 1), GPIO_OUT_LOW)                /* C1 5V Enable */
GPIO(EN_USB_C1_3A,           PIN(3, 5), GPIO_OUT_LOW)                /* C1 3A Enable */
GPIO(EN_USB_C1_CHARGE_EC_L,  PIN(C, 3), GPIO_OUT_LOW)                /* C1 Charge enable */
GPIO(USB_PD_RST_C0,          PIN(0, 3), GPIO_OUT_LOW)                /* C0 PD Reset */
GPIO(USB_C1_PD_RST_ODL,      PIN(7, 4), GPIO_ODR_HIGH)               /* C1 PD Reset */
GPIO(USB_C0_DP_HPD,          PIN(9, 4), GPIO_INPUT)                  /* C0 DP Hotplug Detect */
GPIO(USB_C1_DP_HPD,          PIN(A, 5), GPIO_INPUT)                  /* C1 DP Hotplug Detect */
GPIO(USB2_ID2,               PIN(9, 5), GPIO_OUT_LOW)                /* USB OTG ID */

/* USB Type-A control */
GPIO(USB_A_CHARGE_EN_L,      PIN(0, 0), GPIO_OUT_LOW)
GPIO(EN_USB_A_5V,            PIN(8, 4), GPIO_OUT_LOW)

/* LEDs */
GPIO(CHG_LED1,               PIN(8, 0), GPIO_OUT_LOW)
GPIO(CHG_LED2,               PIN(B, 7), GPIO_OUT_LOW)
GPIO(PWR_LED,                PIN(8, 6), GPIO_OUT_LOW)

/* Alternate functions GPIO definitions */
/* UART pins */
ALTERNATE(PIN_MASK(6, 0x30), 1, MODULE_UART, 0)     /* GPIO64-65 */  /* UART from EC to Servo */

/* I2C pins */
ALTERNATE(PIN_MASK(8, 0x80), 1, MODULE_I2C, 0)      /* GPIO87 */     /* EC_I2C1_3V3_SDA */
ALTERNATE(PIN_MASK(9, 0x01), 1, MODULE_I2C, 0)      /* GPIO90 */     /* EC_I2C1_3V3_SCL */
ALTERNATE(PIN_MASK(9, 0x06), 1, MODULE_I2C, 0)      /* GPIO91-92 */  /* EC_I2C2_PMIC_3V3_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)      /* GPIOB4-B5 */  /* EC_I2C0_0_USBC_3V3_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, 0)      /* GPOPB2-B3 */  /* EC_I2C0_1_3V3_SDA/SCL */
ALTERNATE(PIN_MASK(D, 0x03), 1, MODULE_I2C, 0)      /* GPIOD0-D1 */  /* EC_I2C3_SENSOR_1V8_SDA/SCL */

/* PWM pins */
ALTERNATE(PIN_MASK(B, 0x40), 1, MODULE_PWM, 0)      /* GPIOB6 */     /* PWM KB Backlight */

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)    /* GPIO30-31 */     /* KSI_00-01 */
ALTERNATE(PIN_MASK(2, 0xfc), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)    /* GPIO22-27 */     /* KSI_02-07 */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)   /* GPIO20-21 */     /* KSO_00-01 */
ALTERNATE(PIN_MASK(1, 0x7f), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)   /* GPIO10-16 */     /* KSO_03-09 */
ALTERNATE(PIN_MASK(0, 0xe0), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)   /* GPIO05-07 */     /* KSO_10-12 */
GPIO(KBD_KSO2,               PIN(1, 7), GPIO_OUT_LOW)                /* EC_KSO_02_INV */
