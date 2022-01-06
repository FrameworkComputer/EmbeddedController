/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(POWER_BUTTON_L,     PIN(E, 4), GPIO_INT_BOTH | GPIO_PULL_UP |
	 GPIO_HIB_WAKE_HIGH, power_button_interrupt) /* GSC_EC_PWR_BTN_ODL */
GPIO_INT(LID_OPEN,           PIN(E, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         lid_interrupt)
GPIO_INT(TABLET_MODE_L,      PIN(J, 7), GPIO_INT_BOTH,
         gmr_tablet_switch_isr)

/* Chipset interrupts */
GPIO_INT(AP_EC_WARM_RST_REQ, PIN(D, 3), GPIO_INT_RISING | GPIO_SEL_1P8V,
         chipset_reset_request_interrupt)

/* Power sequencing interrupts */
GPIO_INT(AP_EC_WATCHDOG_L, PIN(C, 7), GPIO_INT_BOTH | GPIO_SEL_1P8V,
         chipset_watchdog_interrupt)
GPIO_INT(AP_IN_SLEEP_L, PIN(F, 2),
         GPIO_INT_BOTH | GPIO_PULL_DOWN | GPIO_SEL_1P8V, power_signal_interrupt)
GPIO_INT(PMIC_EC_PWRGD, PIN(F, 3),
         GPIO_INT_BOTH | GPIO_PULL_DOWN | GPIO_SEL_1P8V, power_signal_interrupt)

/* Sensor Interrupts */
GPIO_INT(BASE_IMU_INT_L,     PIN(M, 3), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         icm42607_interrupt)
/* USB-C interrupts */
GPIO_INT(USB_C0_PPC_INT_ODL, PIN(D, 1), GPIO_INT_BOTH, ppc_interrupt)
GPIO_INT(USB_C0_BC12_INT_ODL,PIN(I, 5), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C1_INT_ODL,     PIN(B, 2), GPIO_INT_FALLING, rt1718s_tcpc_interrupt)
/* TODO: not used in other devices? */
GPIO(LID_ACCEL_INT_L,        PIN(M, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V)

/* Volume button interrupts */
GPIO_INT(VOLUME_DOWN_L,      PIN(D, 5), GPIO_INT_BOTH | GPIO_PULL_UP,
         button_interrupt) /* EC_VOLDN_BTN_ODL */
GPIO_INT(VOLUME_UP_L,        PIN(D, 6), GPIO_INT_BOTH | GPIO_PULL_UP,
         button_interrupt) /* EC_VOLUP_BTN_ODL */

/* Other interrupts */
GPIO_INT(AC_PRESENT,         PIN(E, 5), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,
         extpower_interrupt) /* AC_OK / AC_PRESENT in rev1+ */
GPIO_INT(UART1_RX,           PIN(B, 0), GPIO_INT_FALLING,
         uart_deepsleep_interrupt) /* UART_DEBUG_TX_EC_RX */
GPIO_INT(WP,                 PIN(I, 4), GPIO_INT_BOTH | GPIO_SEL_1P8V,
         switch_interrupt) /* EC_FLASH_WP_OD */
GPIO_INT(SPI0_CS,            PIN(M, 5), GPIO_INT_FALLING,
         spi_event)        /* SPI slave Chip Select -- AP_SPI_EC_CS_L */
GPIO_INT(AP_XHCI_INIT_DONE,  PIN(J, 5), GPIO_INT_BOTH | GPIO_PULL_DOWN | GPIO_SEL_1P8V,
         xhci_init_done_interrupt)

/* Power Sequencing Signals */
GPIO(EC_PMIC_EN_ODL,         PIN(D, 0), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(EC_PMIC_WATCHDOG_L,     PIN(H, 0), GPIO_ODR_LOW | GPIO_SEL_1P8V)
GPIO(EN_PP5000_A,            PIN(C, 6), GPIO_OUT_HIGH)
GPIO(PG_MT6315_PROC_B_ODL,   PIN(E, 1), GPIO_INPUT)
GPIO(PG_MT6360_ODL,          PIN(F, 1), GPIO_INPUT)
GPIO(EN_SLP_Z,               PIN(E, 3), GPIO_OUT_LOW)
GPIO(SYS_RST_ODL,            PIN(B, 6), GPIO_ODR_LOW)
GPIO(EC_BL_EN_OD,            PIN(B, 5), GPIO_ODR_LOW | GPIO_SEL_1P8V)
GPIO(EN_PP3300_SSD,          PIN(H, 1), GPIO_ODR_LOW) /* NVME power sequence control */

/* MKBP event synchronization */
GPIO(EC_INT_L,               PIN(E, 6), GPIO_ODR_HIGH | GPIO_SEL_1P8V) /* EC_AP_INT_ODL */

/* USB and USBC Signals */
GPIO(DP_PATH_SEL,            PIN(G, 0), GPIO_OUT_HIGH)
GPIO(DP_DEMUX_EN,            PIN(G, 1), GPIO_OUT_LOW)
GPIO(EC_AP_DP_HPD_ODL,       PIN(J, 0), GPIO_ODR_HIGH)
GPIO(EN_PP5000_USB_A0_VBUS_X,PIN(B, 7), GPIO_OUT_LOW)
GPIO(USB_C0_DP_IN_HPD,       PIN(H, 4), GPIO_OUT_LOW)
GPIO(USB_C1_DP_IN_HPD,       PIN(J, 1), GPIO_OUT_LOW)
GPIO(USB_C0_FRS_EN,          PIN(F, 0), GPIO_OUT_LOW)

/* Misc Signals */
GPIO(EN_KB_BL,               PIN(A, 6), GPIO_OUT_LOW)
GPIO(EC_BATT_PRES_ODL,       PIN(C, 0), GPIO_INPUT)
GPIO(EN_EC_ID_ODL,           PIN(H, 5), GPIO_ODR_LOW)
GPIO(ENTERING_RW,            PIN(C, 5), GPIO_OUT_LOW) /* EC_ENTERING_RW */
GPIO(EN_5V_USM,              PIN(G, 3), GPIO_OUT_LOW)
GPIO(USB_A0_FAULT_ODL,       PIN(J, 6), GPIO_INPUT)
GPIO(PACKET_MODE_EN,         PIN(D, 4), GPIO_OUT_LOW) /* EC_GSC_PACKET_MODE */

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C_A_SCL,              PIN(B, 3), GPIO_INPUT) /* I2C_CHG_BATT_SCL */
GPIO(I2C_A_SDA,              PIN(B, 4), GPIO_INPUT) /* I2C_CHG_BATT_SDA */
GPIO(I2C_B_SCL,              PIN(C, 1), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_SENSOR_SCL */
GPIO(I2C_B_SDA,              PIN(C, 2), GPIO_INPUT | GPIO_SEL_1P8V) /* I2C_SENSOR_SDA */
GPIO(I2C_C_SCL,              PIN(F, 6), GPIO_INPUT) /* I2C_USB_C0_SCL */
GPIO(I2C_C_SDA,              PIN(F, 7), GPIO_INPUT) /* I2C_USB_C0_SDA */
GPIO(I2C_E_SCL,              PIN(E, 0), GPIO_INPUT) /* I2C_USB_C1_SCL */
GPIO(I2C_E_SDA,              PIN(E, 7), GPIO_INPUT) /* I2C_USB_C1_SDA */
GPIO(I2C_F_SCL,              PIN(A, 4), GPIO_INPUT) /* Rev 2+ I2C_PROG_SCL */
GPIO(I2C_F_SDA,              PIN(A, 5), GPIO_INPUT) /* Rev 2+ I2C_PROG_SDA */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0) /* I2C A */
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, GPIO_SEL_1P8V) /* I2C B */
ALTERNATE(PIN_MASK(F, 0xC0), 1, MODULE_I2C, 0) /* I2C C */
ALTERNATE(PIN_MASK(E, 0x81), 1, MODULE_I2C, 0) /* I2C E */

/* UART */
ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, 0) /* EC to Servo */

/* PWM */
ALTERNATE(PIN_MASK(A, 0b1111), 1, MODULE_PWM, 0) /* PWM 0,1,2,3 */

/* ADC */
ALTERNATE(PIN_MASK(I, 0b11001001), 0, MODULE_ADC, 0) /* ADC 0,3,6,7 */

/* SPI */
ALTERNATE(PIN_MASK(M, 0x33), 0, MODULE_SPI, 0) /* SPI */

/* Unimplemented Pins  */
GPIO(PG_PP5000_S5_OD,        PIN(D, 2), GPIO_INPUT)
/* *_ODL pin has external pullup so don't pull it down. */
GPIO(PG_MT6315_GPU_ODL,      PIN(H, 6), GPIO_INPUT)
/* reserved for future use */
GPIO(CCD_MODE_ODL,           PIN(C, 4), GPIO_INPUT)

/* NC pins, enable internal pull-up/down to avoid floating state. */
GPIO(NC_GPA1,                PIN(A, 1), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(PWM7,                   PIN(A, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(EC_NVME_PLN_ODL,        PIN(D, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_CLK_GPG6,           PIN(G, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC_GPH2,                PIN(H, 2), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(PG_NVME_OD,             PIN(H, 3), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(EN_PP2500_NVME_X,       PIN(J, 2), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(EN_PP1200_NVME_X,       PIN(J, 3), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPJ4,                PIN(J, 4), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(NC_GPM6,                PIN(M, 6), GPIO_INPUT | GPIO_PULL_DOWN)
/*
 * These pins don't have internal pull-down capability,
 * so we set them as output low.
 */
GPIO(SPI_MOSI_GPG4,          PIN(G, 4), GPIO_OUT_LOW)
GPIO(SPI_MISO_GPG5,          PIN(G, 5), GPIO_OUT_LOW)
GPIO(SPI_CS_GPG7,            PIN(G, 7), GPIO_OUT_LOW)
GPIO(EC_ID0,                 PIN(I, 1), GPIO_OUT_LOW)
GPIO(EC_ID1,                 PIN(I, 2), GPIO_OUT_LOW)

/* Other unused pins */
GPIO(NVME_EC_PLA_S3_ODL,     PIN(I, 7), GPIO_INPUT)

