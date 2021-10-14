/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(POWER_BUTTON_L,     PIN(E, 4),
         GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         power_button_interrupt) /* GSC_EC_PWR_BTN_ODL */
GPIO_INT(LID_OPEN,           PIN(E, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         lid_interrupt) /* LID_OPEN_3V3 */
GPIO_INT(TABLET_MODE_L,      PIN(J, 7), GPIO_INT_BOTH,
         gmr_tablet_switch_isr)

/* Chipset interrupts */
GPIO_INT(AP_EC_WARM_RST_REQ, PIN(D, 3), GPIO_INT_RISING | GPIO_SEL_1P8V,
         chipset_reset_request_interrupt)

/* Power sequencing interrupts */
GPIO_INT(AP_IN_SLEEP_L,      PIN(B, 6),
         GPIO_INT_BOTH | GPIO_SEL_1P8V, power_signal_interrupt)

/* Sensor Interrupts */
GPIO_INT(BASE_IMU_INT_L,     PIN(M, 3), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         motion_interrupt)
GPIO_INT(LID_ACCEL_INT_L,    PIN(M, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         lis2dw12_interrupt)

/* Volume button interrupts */
GPIO_INT(VOLUME_DOWN_L,      PIN(D, 5), GPIO_INT_BOTH,
         button_interrupt) /* EC_VOLDN_BTN_ODL */
GPIO_INT(VOLUME_UP_L,        PIN(D, 6), GPIO_INT_BOTH,
         button_interrupt) /* EC_VOLUP_BTN_ODL */

/* Other interrupts */
GPIO_INT(AP_XHCI_INIT_DONE,  PIN(J, 5),
         GPIO_INT_BOTH | GPIO_SEL_1P8V,
         usb_a0_interrupt)
GPIO_INT(AC_PRESENT,         PIN(E, 5), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         extpower_interrupt) /* GSC_ACOK_OD */
GPIO_INT(UART1_RX,           PIN(B, 0), GPIO_INT_FALLING,
         uart_deepsleep_interrupt) /* UART_DEBUG_TX_EC_RX */
GPIO_INT(WP,                 PIN(I, 4), GPIO_INT_BOTH | GPIO_SEL_1P8V,
         switch_interrupt) /* EC_FLASH_WP_OD */
GPIO_INT(SPI0_CS,            PIN(M, 5), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         spi_event)        /* SPI slave Chip Select -- AP_EC_SPI_CS_L */
GPIO_INT(X_EC_GPIO2,         PIN(B, 2), GPIO_INT_BOTH | GPIO_ODR_HIGH,
         x_ec_interrupt)

/* USB-C interrupts */
/* TODO: interrupt function not ready */
GPIO(USB_C0_PPC_BC12_INT_ODL, PIN(D, 1), GPIO_INT_FALLING)
GPIO(USB_C1_BC12_CHARGER_INT_ODL,  PIN(J, 4), GPIO_INT_FALLING)

/* Power Sequencing Signals */
GPIO(EC_PMIC_EN_ODL,         PIN(D, 0), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(EN_PP5000_Z2,           PIN(C, 6), GPIO_OUT_HIGH)
GPIO(EN_ULP,                 PIN(E, 3), GPIO_OUT_LOW)
GPIO(SYS_RST_ODL,            PIN(G, 1), GPIO_ODR_LOW)
GPIO(EC_BL_EN_OD,            PIN(B, 5), GPIO_ODR_LOW | GPIO_SEL_1P8V)
GPIO(AP_EC_SYSRST_ODL,       PIN(J, 2), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(AP_EC_WDTRST_L,         PIN(C, 7), GPIO_INPUT | GPIO_SEL_1P8V)

/* MKBP event synchronization */
GPIO(EC_INT_L,               PIN(E, 6), GPIO_ODR_HIGH | GPIO_SEL_1P8V) /* EC_AP_INT_ODL */

/* USB and USBC Signals */
GPIO(DP_AUX_PATH_SEL,        PIN(G, 0), GPIO_OUT_HIGH)
GPIO(EC_AP_DP_HPD_ODL,       PIN(J, 0), GPIO_ODR_HIGH)
GPIO(EN_PP5000_USB_A0_VBUS,  PIN(B, 7), GPIO_OUT_LOW)
GPIO(USB_C0_PPC_FRSINFO,     PIN(F, 0), GPIO_INPUT)

/* Misc Signals */
GPIO(EC_BATT_PRES_ODL,       PIN(C, 0), GPIO_INPUT)
GPIO(EN_EC_ID_ODL,           PIN(H, 5), GPIO_ODR_LOW)
GPIO(ENTERING_RW,            PIN(C, 5), GPIO_OUT_LOW) /* EC_ENTERING_RW */
GPIO(EN_5V_USM,              PIN(G, 3), GPIO_OUT_LOW)
GPIO(USB_A0_FAULT_ODL,       PIN(J, 6), GPIO_INPUT)

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C_A_SCL,              PIN(B, 3), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_PWR_CBI_SCL */
GPIO(I2C_A_SDA,              PIN(B, 4), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_PWR_CBI_SDA */
GPIO(I2C_B_SCL,              PIN(C, 1), GPIO_INPUT) /* I2C_BATT_SCL_3V3 */
GPIO(I2C_B_SDA,              PIN(C, 2), GPIO_INPUT) /* I2C_BATT_SDA_3V3 */
GPIO(I2C_C_SCL,              PIN(F, 6), GPIO_INPUT) /* I2C_USB_C0_SCL */
GPIO(I2C_C_SDA,              PIN(F, 7), GPIO_INPUT) /* I2C_USB_C0_SDA */
GPIO(I2C_D_SCL,              PIN(F, 2), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_SENSOR_SCL */
GPIO(I2C_D_SDA,              PIN(F, 3), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_SENSOR_SDA */
GPIO(I2C_E_SCL,              PIN(E, 0), GPIO_INPUT) /* I2C_USB_C1_SCL */
GPIO(I2C_E_SDA,              PIN(E, 7), GPIO_INPUT) /* I2C_USB_C1_SDA */
GPIO(I2C_F_SCL,              PIN(A, 4), GPIO_INPUT) /* I2C_PROG_SCL */
GPIO(I2C_F_SDA,              PIN(A, 5), GPIO_INPUT) /* I2C_PROG_SDA */

/* SPI pins - Alternate function below configures SPI module on these pins */

/* Keyboard pins */

/* Subboards HDMI/TYPEC */
GPIO(EC_X_GPIO1,             PIN(H, 4), GPIO_OUT_LOW)
GPIO(EC_X_GPIO3,             PIN(J, 1), GPIO_INPUT)
GPIO(HDMI_PRSNT_ODL,         PIN(J, 3), GPIO_INPUT) /* low -> hdmi, other -> usb */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0) /* I2C A */
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, GPIO_SEL_1P8V) /* I2C B */
ALTERNATE(PIN_MASK(F, 0xCC), 1, MODULE_I2C, 0) /* I2C C, D */
ALTERNATE(PIN_MASK(E, 0x81), 1, MODULE_I2C, 0) /* I2C E */

/* UART */
ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, 0) /* EC to Servo */

/* PWM */
ALTERNATE(PIN_MASK(A, 0x07), 1, MODULE_PWM, 0) /* PWM 0~2 */

/* ADC */
ALTERNATE(PIN_MASK(I, 0b10010111), 0, MODULE_ADC, 0) /* ADC 0,1,2,4,7 */

/* SPI */
ALTERNATE(PIN_MASK(M, 0x33), 0, MODULE_SPI, GPIO_SEL_1P8V) /* SPI */

/* Unimplemented Pins  */
GPIO(PACKET_MODE_EN,         PIN(D, 4), GPIO_OUT_LOW)
GPIO(PG_PP5000_Z2_OD,        PIN(D, 2), GPIO_INPUT)
GPIO(PG_MT6315_PROC_B_ODL,   PIN(E, 1), GPIO_INPUT)
GPIO(EC_PEN_CHG_DIS_ODL,     PIN(H, 3), GPIO_ODR_HIGH) /* 5V output */
/* reserved for future use */
GPIO(CCD_MODE_ODL,           PIN(C, 4), GPIO_INPUT)

/* NC pins, enable internal pull-up/down to avoid floating state. */
GPIO(NC_GPA3,                PIN(A, 3), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPA6,                PIN(A, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPA7,                PIN(A, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPC3,                PIN(C, 3), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPD7,                PIN(D, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPF1,                PIN(F, 1), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_CLK_GPG6,           PIN(G, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC_GPH0,                PIN(H, 0), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPH6,                PIN(H, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPI7,                PIN(I, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPM6,                PIN(M, 6), GPIO_INPUT | GPIO_PULL_DOWN)
/*
 * These pins don't have internal pull-down capability,
 * so we set them as output low.
 */
GPIO(SPI_MOSI_GPG4,          PIN(G, 4), GPIO_OUT_LOW)
GPIO(SPI_MISO_GPG5,          PIN(G, 5), GPIO_OUT_LOW)
GPIO(SPI_CS_GPG7,            PIN(G, 7), GPIO_OUT_LOW)

/* pins used in power/mt8192, will be removed after mt8186 code ready */
UNIMPLEMENTED(AP_EC_WATCHDOG_L)
UNIMPLEMENTED(EC_PMIC_WATCHDOG_L)
UNIMPLEMENTED(PMIC_EC_PWRGD)
