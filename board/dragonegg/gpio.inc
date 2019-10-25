/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,        PIN(F, 3), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(WP_L,            PIN(F, 1), GPIO_INT_BOTH, switch_interrupt)        /* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L,  PIN(E, 2), GPIO_INT_BOTH, power_button_interrupt)  /* MECH_PWR_BTN_ODL */
GPIO_INT(ACOK_OD,         PIN(E, 3), GPIO_INT_BOTH, extpower_interrupt)
#ifdef CONFIG_LOW_POWER_IDLE
/* Used to wake up the EC from Deep Doze mode when writing to console */
GPIO_INT(UART1_RX,        PIN(B, 0), GPIO_INT_BOTH, uart_deepsleep_interrupt) /* UART_SERVO_TX_EC_RX */
#endif

/* Power sequencing interrupts */
GPIO_INT(SLP_S0_L,        PIN(E, 4), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_SIGNALS
GPIO_INT(SLP_S3_L,        PIN(I, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S4_L,        PIN(I, 5), GPIO_INT_BOTH, power_signal_interrupt)
#endif
GPIO_INT(SLP_SUS_L,       PIN(G, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_RSMRST_ODL,PIN(E, 1), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_DSW_PWROK, PIN(D, 6), GPIO_INT_BOTH, power_signal_interrupt)

/* USB-C interrupts */
GPIO_INT(USB_C0_TCPPC_INT_L,   PIN(B, 2), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C2_TCPPC_INT_ODL, PIN(L, 1), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C2_TCPC_INT_ODL,  PIN(K, 6), GPIO_INT_FALLING, tcpc_alert_event)

/* Misc. interrupts */
GPIO_INT(VOLUME_DOWN_L,    PIN(I, 6), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(VOLUME_UP_L,      PIN(I, 7), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
/* TODO (b:110947310) Uncomment this when sensor task is added to image */
/* GPIO_INT(BASE_SIXAXIS_INT_L,	PIN(J, 0), GPIO_INT_FALLING | GPIO_SEL_1P8V, lsm6dsm_interrupt) */

#ifdef CONFIG_HOSTCMD_ESPI
/* enable 1.8v input of EC's espi_reset pin, and then this pin takes effect. */
GPIO_INT(ESPI_RESET_L,    PIN(D, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V, espi_reset_pin_asserted_interrupt) /* eSPI_reset# */
#endif

GPIO(SYS_RESET_L,         PIN(D, 1), GPIO_ODR_HIGH)  /* SYS_RST_ODL */
GPIO(PCH_WAKE_L,          PIN(D, 5), GPIO_ODR_HIGH)  /* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L,        PIN(B, 6), GPIO_ODR_HIGH)  /* EC_PCH_PWR_BTN_ODL */

/* Power Sequencing Signals */
/*
 * Both PP5000 and PP3300_TCPC should be powered up whenever the EC is not in
 * hibernate mode. They are enabled by default here and disabled in the
 * board_hibernate() callback when the EC goes into hibernate mode.
 */
GPIO(EN_PP5000,           PIN(K, 5), GPIO_OUT_HIGH)
GPIO(EN_PP3300_TCPC,      PIN(E, 6), GPIO_OUT_HIGH)
GPIO(EN_PP3300_A,         PIN(C, 5), GPIO_OUT_LOW)
GPIO(EC_PCH_DSW_PWROK,    PIN(L, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_RSMRST_L,     PIN(H, 0), GPIO_OUT_LOW)
GPIO(EC_PROCHOT_ODL,      PIN(D, 0), GPIO_ODR_HIGH)
GPIO(PP5000_PG_OD,        PIN(F, 0), GPIO_INPUT)

/* USB and USBC Signals */
GPIO(USB_OC_ODL,          PIN(J, 6), GPIO_ODR_HIGH)
GPIO(EN_USB_A_5V,         PIN(G, 6), GPIO_OUT_LOW)
GPIO(EN_USB_A_HIGH_POWER, PIN(D, 4), GPIO_OUT_LOW)

/* BC 1.2 Detection Signals */
GPIO(USB_C0_BC12_CHG_MAX,     PIN(D, 3), GPIO_INPUT)     /* C0 BC 1.2 CDP signal */
GPIO(USB_C1_BC12_CHG_MAX,     PIN(B, 7), GPIO_INPUT)     /* C1 BC 1.2 CDP signal */
GPIO(USB_C2_BC12_CHG_MAX,     PIN(K, 0), GPIO_INPUT)     /* C2 BC 1.2 CDP signal */
GPIO(USB_C0_BC12_VBUS_ON_ODL, PIN(C, 0), GPIO_ODR_HIGH)  /* C0 BC 1.2 enable signal */
GPIO(USB_C1_BC12_VBUS_ON_ODL, PIN(E, 5), GPIO_ODR_HIGH)  /* C1 BC 1.2 enable signal */
GPIO(USB_C2_BC12_VBUS_ON_ODL, PIN(K, 1), GPIO_ODR_HIGH)  /* C2 BC 1.2 enable signal */

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C0_SCL,            PIN(B, 3), GPIO_INPUT)     /* EC_PROG_SCL */
GPIO(I2C0_SDA,            PIN(B, 4), GPIO_INPUT)     /* EC_PROG_SDA */
GPIO(I2C1_SCL,            PIN(C, 1), GPIO_INPUT)     /* EC_I2C_SENSOR_KB_BL_SCL */
GPIO(I2C1_SDA,            PIN(C, 2), GPIO_INPUT)     /* EC_I2C_SENSOR_KB_BL_SDA */
GPIO(I2C2_SCL,            PIN(F, 6), GPIO_INPUT)     /* EC_I2C_USB_C1C2_SCL */
GPIO(I2C2_SDA,            PIN(F, 7), GPIO_INPUT)     /* EC_I2C_USB_C1C2_SDA */
GPIO(I2C4_SCL,            PIN(E, 0), GPIO_INPUT)     /* EC_I2C_USB_C0_SCL */
GPIO(I2C4_SDA,            PIN(E, 7), GPIO_INPUT)     /* EC_I2C_USB_C0_SDA */
GPIO(I2C5_SCL,            PIN(A, 4), GPIO_INPUT)     /* EC_I2C_POWER_SCL */
GPIO(I2C5_SDA,            PIN(A, 5), GPIO_INPUT)     /* EC_I2C_POWER_SDA */

GPIO(CCD_MODE_ODL,        PIN(H, 5), GPIO_INPUT)     /* H1 Case Closed Debug */
GPIO(CHG_VAP_OTG_EN,      PIN(C, 3), GPIO_OUT_LOW)   /* Charger VAP/OTG Mode_*/
GPIO(EC_BATT_PRES_ODL,    PIN(H, 3), GPIO_INPUT)
GPIO(ENTERING_RW,         PIN(G, 0), GPIO_OUT_LOW)   /* EC_ENTERING_RW */
GPIO(EDP_BKTLEN_OD,       PIN(K, 4), GPIO_ODR_LOW)   /* Backlight Disable */
GPIO(KB_BL_EN,            PIN(J, 3), GPIO_OUT_LOW)
/* TODO (b:110947310) Convert this to interrupt when senor support is added */
GPIO(LID_ACCEL_INT_L,     PIN(J, 1), GPIO_INPUT | GPIO_SEL_1P8V)

/* Board ID */
GPIO(BOARD_VERSION1,      PIN(H, 4), GPIO_INPUT)     /* Board ID bit0 */
GPIO(BOARD_VERSION2,      PIN(K, 3), GPIO_INPUT)     /* Board ID bit1 */
GPIO(BOARD_VERSION3,      PIN(H, 6), GPIO_INPUT)     /* Board ID bit2 */

/* Fan Control Pins (connected to TP only, set as inputs) */
GPIO(EC_FAN_TACH1,        PIN(J, 2), GPIO_INPUT)
GPIO(EC_FAN_TACH2,        PIN(D, 7), GPIO_INPUT)
GPIO(FAN_PWM1,            PIN(A, 6), GPIO_INPUT)
GPIO(FAN_PWM2,            PIN(A, 7), GPIO_INPUT)

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x03), 0, MODULE_UART, 0)            /* UART from EC to Servo */
ALTERNATE(PIN_MASK(B, 0x18), 0, MODULE_I2C, 0)             /* I2C0 */
ALTERNATE(PIN_MASK(C, 0x06), 0, MODULE_I2C, 0)             /* I2C1 */
ALTERNATE(PIN_MASK(F, 0xC0), 0, MODULE_I2C, 0)             /* I2C2 */
ALTERNATE(PIN_MASK(E, 0x81), 0, MODULE_I2C, 0)             /* I2C4 */
ALTERNATE(PIN_MASK(A, 0x30), 0, MODULE_I2C, 0)             /* I2C5 */
/* Charger_IADP -> ADC15, CHARGER_PSYS -> ADC16 */
ALTERNATE(PIN_MASK(L, 0x0C), 0, MODULE_ADC, 0)
/* Temp sensors 1 -> ADC2, 2 -> ADC3, 3 -> ADC13 */
ALTERNATE(PIN_MASK(I, 0x0C), 0, MODULE_ADC, 0)
ALTERNATE(PIN_MASK(L, 0x01), 0, MODULE_ADC, 0)
/* Keyboard Backlight Control */
ALTERNATE(PIN_MASK(A, 0x01), 0, MODULE_PWM, 0)
/* 3 Color LED Control */
ALTERNATE(PIN_MASK(A, 0x0E), 0, MODULE_PWM, 0)
ALTERNATE(PIN_MASK(I, 0x03), 0, MODULE_ADC, 0)		/* ADC1 & ADC0: ADC_USB_C0_VBUS & ADC_USB_C1_VBUS */
