/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(POWER_BUTTON_L,     PIN(E, 4), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
	power_button_interrupt) /* H1_EC_PWR_BTN_ODL */
GPIO_INT(LID_OPEN,           PIN(E, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         lid_interrupt)
GPIO_INT(TABLET_MODE_L,      PIN(J, 7), GPIO_INT_BOTH,
         gmr_tablet_switch_isr)

/* Power sequencing interrupts */
GPIO_INT(AP_EC_WARM_RST_REQ, PIN(D, 3), GPIO_INT_RISING | GPIO_SEL_1P8V,
         chipset_reset_request_interrupt)
GPIO_INT(AP_EC_SHUTDOWN_REQ_L, PIN(F, 2),
         GPIO_INT_BOTH | GPIO_PULL_DOWN | GPIO_SEL_1P8V,
         chipset_reset_request_interrupt)
GPIO_INT(PG_S5_PWR_OD,           PIN(A, 6), GPIO_INT_BOTH,
	 power_signal_interrupt)
GPIO_INT(PG_VDD1_VDD2_OD,        PIN(M, 6), GPIO_INT_BOTH | GPIO_SEL_1P8V,
	 power_signal_interrupt)
GPIO_INT(PG_VDD_MEDIA_ML_OD,     PIN(J, 3), GPIO_INT_BOTH,
	 power_signal_interrupt)
GPIO_INT(PG_VDD_SOC_OD,          PIN(J, 4), GPIO_INT_BOTH,
	 power_signal_interrupt)
GPIO_INT(PG_VDD_DDR_OD,          PIN(J, 5), GPIO_INT_BOTH,
	 power_signal_interrupt)

/* Sensor Interrupts */
GPIO_INT(BASE_IMU_INT_L,     PIN(M, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         bmi160_interrupt)
GPIO_INT(LID_ACCEL_INT_L,    PIN(G, 0), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         lis2dw12_interrupt)

/* USB-C interrupts */
GPIO_INT(USB_C0_FAULT_ODL,   PIN(D, 1), GPIO_INT_BOTH, ppc_interrupt)
GPIO_INT(USB_C1_FAULT_ODL,   PIN(B, 2), GPIO_INT_BOTH, ppc_interrupt)

/* Volume button interrupts */
GPIO_INT(EC_VOLUP_BTN_ODL,       PIN(D, 5), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(EC_VOLDN_BTN_ODL,       PIN(H, 6), GPIO_INT_BOTH, button_interrupt)

/* Other interrupts */
GPIO_INT(ACOK_OD,            PIN(E, 5), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         extpower_interrupt) /* AC_OK / AC_PRESENT in rev1+ */
GPIO_INT(UART1_RX,           PIN(B, 0), GPIO_INT_FALLING,
         uart_deepsleep_interrupt) /* UART_DEBUG_TX_EC_RX */
GPIO_INT(WP,                 PIN(I, 4), GPIO_INT_BOTH | GPIO_SEL_1P8V,
         switch_interrupt) /* EC_FLASH_WP_OD */
GPIO_INT(SPI0_CS,            PIN(M, 5), GPIO_INT_FALLING,
         spi_event)        /* SPI slave Chip Select -- AP_SPI_EC_CS_L */

/* Power Sequencing Signals */
GPIO(EN_PP1800_S5,           PIN(H, 0), GPIO_OUT_LOW)
GPIO(EN_PP5000_S5,           PIN(C, 6), GPIO_OUT_LOW)
GPIO(EN_VDD_SOC,             PIN(E, 1), GPIO_OUT_LOW)
GPIO(EN_VDD_MEDIA_ML,        PIN(F, 1), GPIO_OUT_LOW)
GPIO(SYS_RST_ODL,            PIN(B, 6), GPIO_ODR_LOW)
GPIO(EC_BL_EN_OD,            PIN(B, 5), GPIO_ODR_LOW | GPIO_SEL_1P8V)
GPIO(EN_VDDQ_VR_D,           PIN(M, 3), GPIO_OUT_LOW)
GPIO(EN_PP1820A_IO_X,        PIN(D, 4), GPIO_OUT_LOW)
GPIO(EN_PP1800_VDD33_PMC_X,  PIN(D, 0), GPIO_OUT_LOW)
GPIO(EN_PP0800_VDD_PMC_X,    PIN(D, 2), GPIO_OUT_LOW)
GPIO(EN_VDD1_VDD2_VR,        PIN(A, 7), GPIO_OUT_LOW)
GPIO(EN_PP3300A_IO_X,        PIN(D, 7), GPIO_OUT_LOW)
GPIO(EN_VDD_DDR,             PIN(I, 0), GPIO_OUT_LOW)
GPIO(EN_VDD_CPU,             PIN(F, 3), GPIO_OUT_LOW)
GPIO(EN_PP1800_S3,           PIN(G, 2), GPIO_OUT_LOW)
GPIO(EN_PP3300_S3,           PIN(G, 3), GPIO_OUT_LOW)
GPIO(EN_VDD_GPU,             PIN(G, 1), GPIO_OUT_LOW)
GPIO(EN_PP1800_VDDIO_PMC_X,    PIN(B, 7), GPIO_OUT_LOW)

/* MKBP event synchronization */
GPIO(EC_INT_L,               PIN(E, 6), GPIO_ODR_HIGH | GPIO_SEL_1P8V) /* EC_AP_INT_ODL */

/* USB and USBC Signals */
GPIO(USB_C0_HPD_3V3,         PIN(J, 0), GPIO_OUT_LOW)
GPIO(USB_C1_HPD_3V3,         PIN(J, 1), GPIO_OUT_LOW)
GPIO(USB_C1_HPD_IN,          PIN(J, 2), GPIO_OUT_LOW)
GPIO(EN_USB_C1_MUX_PWR,      PIN(F, 0), GPIO_OUT_LOW)
GPIO(USB_C0_SBU1_DC,         PIN(H, 4), GPIO_OUT_LOW)
GPIO(USB_C0_SBU2_DC,         PIN(H, 5), GPIO_OUT_LOW)

/* Misc Signals */
GPIO(EC_BATT_PRES_ODL,       PIN(C, 0), GPIO_INPUT)
GPIO(ENTERING_RW,            PIN(C, 5), GPIO_OUT_LOW) /* EC_ENTERING_RW */
GPIO(FAN_SPEED_TACH_1,       PIN(D, 6), GPIO_INPUT)
GPIO(EN_EC_ID_ODL,           PIN(C, 3), GPIO_OUT_LOW)
GPIO(EN_PP5000_FAN_X,        PIN(E, 3), GPIO_OUT_LOW)
GPIO(EN_PPVAR_KB_BL_X,       PIN(H, 3), GPIO_OUT_LOW)

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C_A_SCL,              PIN(B, 3), GPIO_INPUT) /* I2C_CHG_BATT_SCL */
GPIO(I2C_A_SDA,              PIN(B, 4), GPIO_INPUT) /* I2C_CHG_BATT_SDA */
GPIO(I2C_B_SCL,              PIN(C, 1), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_SENSOR_SCL */
GPIO(I2C_B_SDA,              PIN(C, 2), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_SENSOR_SDA */
GPIO(I2C_C_SCL,              PIN(F, 6), GPIO_INPUT) /* I2C_USB_C0_SCL */
GPIO(I2C_C_SDA,              PIN(F, 7), GPIO_INPUT) /* I2C_USB_C0_SCL */
GPIO(I2C_E_SCL,              PIN(E, 0), GPIO_INPUT) /* I2C_USB_C1_SCL */
GPIO(I2C_E_SDA,              PIN(E, 7), GPIO_INPUT) /* I2C_USB_C1_SDA */

/* SPI pins - Alternate function below configures SPI module on these pins */

/* NC / TP */

/* Keyboard pins */

/* CLOCK */
GPIO(CLK_32_EC,              PIN(J, 6), GPIO_INPUT)

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0) /* I2C A */
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, GPIO_SEL_1P8V) /* I2C B */
ALTERNATE(PIN_MASK(F, 0xC0), 1, MODULE_I2C, 0) /* I2C C */
ALTERNATE(PIN_MASK(E, 0x81), 1, MODULE_I2C, 0) /* I2C E */

/* UART */
ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, 0) /* EC to Servo */

/* PWM */
ALTERNATE(PIN_MASK(A, 0x0F), 1, MODULE_PWM, 0) /* PWM 0~3 */

/* ADC */
ALTERNATE(PIN_MASK(I, 0x6E), 0, MODULE_ADC, 0) /* ADC 1,2,3,5,6 */

/* SPI */
ALTERNATE(PIN_MASK(M, 0x33), 0, MODULE_SPI, 0) /* SPI */

/* Unimplemented Pins  */
UNIMPLEMENTED(AP_EC_WATCHDOG_L)

/* *_ODL pin has external pullup so don't pull it down. */

/* reserved for future use */
GPIO(CCD_MODE_ODL,           PIN(C, 4), GPIO_INPUT)
/*
 * ADC pins don't have internal pull-down capability,
 * so we set them as output low.
 */
GPIO(NC_GPI7,                PIN(I, 7), GPIO_OUT_LOW)
/* NC pins, enable internal pull-up/down to avoid floating state. */
GPIO(SPI_CLK_GPG6,           PIN(G, 6), GPIO_INPUT | GPIO_PULL_UP)
/*
 * These 3 pins don't have internal pull-down capability,
 * so we set them as output low.
 */
GPIO(SPI_MOSI_GPG4,          PIN(G, 4), GPIO_OUT_LOW)
GPIO(SPI_MISO_GPG5,          PIN(G, 5), GPIO_OUT_LOW)
GPIO(SPI_CS_GPG7,            PIN(G, 7), GPIO_OUT_LOW)
