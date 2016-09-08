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


GPIO_INT(WP_L,             PIN(9, 3), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(SHI_CS_L,         PIN(5, 3), GPIO_INT_FALLING | GPIO_PULL_DOWN,
         shi_cs_event)
GPIO_INT(USB_C0_PD_INT_L,  PIN(6, 0), GPIO_INT_FALLING | GPIO_PULL_UP,
         tcpc_alert_event)
GPIO_INT(USB_C1_PD_INT_L,  PIN(6, 2), GPIO_INT_FALLING | GPIO_PULL_UP,
         tcpc_alert_event)
GPIO_INT(VOLUME_UP_L,      PIN(8, 2), GPIO_INT_BOTH | GPIO_PULL_UP,
         button_interrupt)
GPIO_INT(VOLUME_DOWN_L,    PIN(8, 3), GPIO_INT_BOTH | GPIO_PULL_UP,
         button_interrupt)
GPIO_INT(POWER_BUTTON_L,   PIN(0, 4), GPIO_INT_BOTH | GPIO_PULL_UP,
         power_button_interrupt)
GPIO_INT(LID_OPEN,         PIN(9, 7), GPIO_INT_BOTH | GPIO_SEL_1P8V,
         lid_interrupt)

GPIO_INT(PP5000_PG,        PIN(7, 1), GPIO_INT_BOTH | GPIO_PULL_UP,
         power_signal_interrupt)
GPIO_INT(TPS65261_PG,      PIN(7, 5), GPIO_INT_BOTH | GPIO_PULL_UP,
         power_signal_interrupt)
/* TODO: Remove PD in S3 for power savings */
GPIO_INT(AP_EC_S3_S0_L,    PIN(C, 1), GPIO_INT_BOTH | GPIO_PULL_DOWN,
         power_signal_interrupt)
GPIO_INT(AP_CORE_PG,       PIN(6, 7), GPIO_INT_BOTH | GPIO_PULL_UP,
         power_signal_interrupt)
GPIO_INT(WARM_RESET_REQ,   PIN(7, 3), GPIO_INT_RISING | GPIO_PULL_DOWN,
         warm_reset_request_interrupt)
GPIO_INT(AP_OVERTEMP,      PIN(7, 4), GPIO_INT_RISING | GPIO_PULL_DOWN,
         overtemp_interrupt)
GPIO_INT(CHARGER_INT_L,    PIN(3, 3), GPIO_INT_FALLING | GPIO_PULL_UP,
         bd99955_vbus_interrupt)
GPIO_INT(BASE_SIXAXIS_INT_L,PIN(4, 0), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         bmi160_interrupt)

/* VR EN */
GPIO(AP_CORE_EN,           PIN(7, 2), GPIO_OUT_LOW)
GPIO(LPDDR_PWR_EN,         PIN(8, 6), GPIO_OUT_LOW)
GPIO(PPVAR_CLOGIC_EN,      PIN(C, 5), GPIO_OUT_LOW)
GPIO(PPVAR_LOGIC_EN,       PIN(8, 5), GPIO_OUT_LOW)

GPIO(PP900_AP_EN,          PIN(B, 7), GPIO_OUT_LOW)
GPIO(PP900_DDRPLL_EN,      PIN(C, 0), GPIO_OUT_LOW)
GPIO(PP900_PLL_EN,         PIN(5, 4), GPIO_OUT_LOW)
GPIO(PP900_PMU_EN,         PIN(C, 2), GPIO_OUT_LOW)
GPIO(PP900_USB_EN,         PIN(A, 5), GPIO_OUT_LOW)
GPIO(PP900_PCIE_EN,        PIN(0, 0), GPIO_OUT_LOW)

/* NC */
GPIO(GPIO81_NC,            PIN(8, 1), GPIO_INPUT | GPIO_PULL_UP)

GPIO(PP1800_SENSOR_EN_L,   PIN(A, 7), GPIO_OUT_HIGH)
GPIO(PP1800_USB_EN_L,      PIN(A, 6), GPIO_OUT_HIGH)
GPIO(PP1800_LID_EN_L,      PIN(B, 0), GPIO_OUT_HIGH)
GPIO(PP1800_PMU_EN_L,      PIN(5, 1), GPIO_OUT_HIGH)
GPIO(PP1800_AP_AVDD_EN_L,  PIN(5, 2), GPIO_OUT_HIGH)
GPIO(PP1800_S0_EN_L,       PIN(5, 0), GPIO_OUT_HIGH)
GPIO(PP1800_SIXAXIS_EN_L,  PIN(5, 6), GPIO_OUT_HIGH)

GPIO(PP3300_TRACKPAD_EN_L, PIN(3, 2), GPIO_OUT_HIGH)
GPIO(PP3300_USB_EN_L,      PIN(3, 7), GPIO_OUT_HIGH)

GPIO(PP5000_EN,            PIN(C, 6), GPIO_OUT_LOW)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C0_SCL0,            PIN(B, 5), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C0_SDA0,            PIN(B, 4), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C0_SCL1,            PIN(B, 3), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C0_SDA1,            PIN(B, 2), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C1_SCL,             PIN(9, 0), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C1_SDA,             PIN(8, 7), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C2_SCL,             PIN(9, 2), GPIO_ODR_HIGH)
GPIO(I2C2_SDA,             PIN(9, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SCL,             PIN(D, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SDA,             PIN(D, 0), GPIO_ODR_HIGH)

/* Attached to push-pull interrupt pin of accel, but unused */
GPIO(LID_ACCEL_INT_L,      PIN(C, 7), GPIO_INPUT)

/* KSO2 is inverted */
GPIO(KBD_KSO2,             PIN(1, 7), GPIO_OUT_LOW)

GPIO(USB_C0_5V_EN,       PIN(D, 3), GPIO_OUT_LOW)
GPIO(USB_C1_5V_EN,       PIN(D, 2), GPIO_OUT_LOW)

GPIO(ENTERING_RW,          PIN(7, 6), GPIO_OUT_LOW)
GPIO(SYS_RST_L,            PIN(6, 1), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)
GPIO(EC_INT_L,             PIN(5, 7), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)
GPIO(EC_BOARD_ID_EN_L,     PIN(3, 5), GPIO_OUT_HIGH)

GPIO(USB_DP_HPD,           PIN(6, 6), GPIO_OUT_LOW)
GPIO(CHARGER_RESET_L,      PIN(0, 1), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)
GPIO(TPM_ALLOW_RST,        PIN(0, 2), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)
GPIO(EC_BATT_PRES_L,       PIN(3, 4), GPIO_INPUT)
GPIO(LID_360_L,            PIN(3, 6), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(CCD_MODE_ODL,         PIN(6, 3), GPIO_INPUT | GPIO_PULL_UP)
GPIO(PP3300_S0_EN_L,       PIN(7, 0), GPIO_OUT_HIGH)
GPIO(SPI_SENSOR_CS_L,      PIN(9, 4), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)

GPIO(USB_C0_DISCHARGE,     PIN(0, 3), GPIO_OUT_LOW)
GPIO(USB_C1_DISCHARGE,     PIN(B, 1), GPIO_OUT_LOW)

#if defined(BOARD_KEVIN)
GPIO(GPIO84_NC,            PIN(8, 4), GPIO_INPUT | GPIO_PULL_UP)
#elif defined(BOARD_GRU)
GPIO(USB_A_EN,             PIN(C, 3), GPIO_OUT_LOW)
GPIO(USB_A_CHARGE_EN,      PIN(8, 4), GPIO_OUT_LOW)
GPIO(GPIOB6_NC,            PIN(B, 6), GPIO_INPUT | GPIO_PULL_UP)
#endif

/*
 * SPI host interface - enable PDs by default. These will be made functional
 * by the SHI driver when the AP powers up, and restored back to GPIO when
 * the AP powers down.
 */
GPIO(SHI_SDI,              PIN(4, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SHI_SDO,              PIN(4, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SHI_SCLK,             PIN(5, 5), GPIO_INPUT | GPIO_PULL_DOWN)

/* SPIP_MOSI/SPIP_SCLK GPIOA3/A1 */
ALTERNATE(PIN_MASK(A, 0x0A), 1,  MODULE_SPI,           0)
/* SPIP_MISO GPIO95 */
ALTERNATE(PIN_MASK(9, 0x20), 1,  MODULE_SPI,           0)
/* I2C0SDA1/I2C0SCL1 GPIOB2/B3 */
ALTERNATE(PIN_MASK(B, 0x0C), 1,  MODULE_I2C,           0)
/* I2C0SDA0/I2C0SCL0 GPIOB4/B5 */
ALTERNATE(PIN_MASK(B, 0x30), 1,  MODULE_I2C,           0)
/* I2C1SDA GPIO87 */
ALTERNATE(PIN_MASK(8, 0x80), 1,  MODULE_I2C,           0)
/* I2C1SCL/I2C2SDA/I2C2SCL GPIO90/91/92 */
ALTERNATE(PIN_MASK(9, 0x07), 1,  MODULE_I2C,           0)
/* I2C3SDA/I2C3SCL GPIOD0/D1    */
ALTERNATE(PIN_MASK(D, 0x03), 1,  MODULE_I2C,           0)
/* PWM2 / BLPWM */
ALTERNATE(PIN_MASK(C, 0x10), 1,  MODULE_PWM,           0)
/* PWM3 / LED_RED(net LED_CHARGE) */
ALTERNATE(PIN_MASK(8, 0x01), 1,  MODULE_PWM,           0)

/* Kevin-only LEDs */
#ifdef BOARD_KEVIN
/* PWM0 / LED_GREEN(net LED_ACIN) */
ALTERNATE(PIN_MASK(C, 0x08), 1,  MODULE_PWM,           0)
/* PWM4 / LED_BLUE(net LED_POWER) */
ALTERNATE(PIN_MASK(B, 0x40), 1,  MODULE_PWM,           0)
#endif

/* CR_SIN/SOUT GPIO64/65 */
ALTERNATE(PIN_MASK(6, 0x30), 1,  MODULE_UART,          GPIO_PULL_UP)
/* ADC0-4 */
ALTERNATE(PIN_MASK(4, 0x3e), 1,  MODULE_ADC,           0)

/* Keyboard Columns */
ALTERNATE(PIN_MASK(0, 0xE0), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(1, 0x7F), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(2, 0x03), 0,  MODULE_KEYBOARD_SCAN, 0)

/* Keyboard Rows */
ALTERNATE(PIN_MASK(2, 0xFC), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(3, 0x03), 0,  MODULE_KEYBOARD_SCAN, 0)

/* External 32KHz input clock - GPIOE7 */
ALTERNATE(PIN_MASK(E, 0x80), 1,  MODULE_CLOCK,         0)
