/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(CHGR_ACOK,            PIN(D, 4),  GPIO_INT_BOTH,    vbus_evt)
GPIO_INT(POWER_BUTTON_L,       PIN(C, 13), GPIO_INT_BOTH,                   power_button_interrupt) /* active high, the name is for compatibility with existing code */
GPIO_INT(USBC_BC12_INT_L,      PIN(D, 11), GPIO_INT_FALLING | GPIO_PULL_UP, usb_evt)
GPIO_INT(LID_OPEN,             PIN(E, 1),  GPIO_INT_BOTH | GPIO_PULL_UP,    lid_interrupt)
GPIO_INT(CHARGE_DONE,          PIN(E, 6),  GPIO_INT_BOTH,                   inductive_charging_interrupt)
GPIO_INT(AP_IN_SUSPEND,        PIN(F, 9),  GPIO_INT_BOTH,                   power_signal_interrupt)
GPIO_INT(AP_HOLD,              PIN(E, 3),  GPIO_INT_BOTH,                   power_signal_interrupt)
GPIO_INT(ACC_IRQ1,             PIN(D, 12), GPIO_INT_FALLING,                bmi160_interrupt)

/* Interrupt lines not used yet */
GPIO(BC_TEMP_ALERT_L,      PIN(C, 5),  GPIO_INT_FALLING)
GPIO(LB_INT_L,             PIN(C, 14), GPIO_INT_FALLING | GPIO_PULL_UP)
GPIO(LIGHTBAR_EN_L,        PIN(E, 8),  GPIO_INT_FALLING | GPIO_PULL_UP)
GPIO(BASE_PRES_L,          PIN(E, 10), GPIO_INT_BOTH | GPIO_PULL_UP)
GPIO(CHGR_INT_L,           PIN(B, 2),  GPIO_INT_FALLING)
GPIO(CAM_SYNC_INT_L,       PIN(C, 7),  GPIO_INT_FALLING)
GPIO(COMPASS_DRDY,         PIN(A, 8),  GPIO_INPUT)

/* Buttons */
GPIO(BTN_VOLD_L,           PIN(C, 0),  GPIO_INPUT | GPIO_PULL_UP)
GPIO(BTN_VOLU_L,           PIN(A, 2),  GPIO_INPUT | GPIO_PULL_UP)

/* PD RX/TX */
GPIO(USBC_CC1_PD,          PIN(A, 1),  GPIO_ANALOG)
GPIO(USBC_CC2_PD,          PIN(A, 3),  GPIO_ANALOG)
GPIO(USBC_CC_EN_0,         PIN(E, 7),  GPIO_OUT_LOW) /* on rev v0+ */
GPIO(USBC_CC_TX_DATA,      PIN(A, 6),  GPIO_OUT_LOW)
GPIO(USBC_CC_TX_EN,        PIN(D, 7),  GPIO_OUT_LOW)

GPIO(SPI3_NSS,             PIN(A, 4),  GPIO_OUT_LOW) /* USB_CC_EN on v6/7/8 */
#if 0
/* Alternate functions */
GPIO(USBC_TX_CLKOUT,       PIN(B, 1),  GPIO_OUT_LOW)
GPIO(USBC_TX_CLKIN,        PIN(B, 3),  GPIO_OUT_LOW)
#endif

/* System power */
GPIO(PMIC_PWRON_L,         PIN(D, 14), GPIO_ODR_HIGH)
GPIO(PMIC_WARM_RESET_L,    PIN(E, 4),  GPIO_ODR_HIGH)
/*
 * We are missing an external pull-up for EN_PP3300.
 * This GPIO is used to pull it up through an external 100kOhm.
 * EN_PP3300 is still controlled by PMIC though.
 */
GPIO(EN_PP3300_RSVD,       PIN(E, 13), GPIO_OUT_LOW)
/* sensor temp output and PMIC reset input */
GPIO(PMIC_THERM_L,         PIN(B, 8),  GPIO_ODR_HIGH)
GPIO(VBUS_SENSE,           PIN(A, 0),  GPIO_ANALOG)
GPIO(CHGR_PSEL,            PIN(B, 0),  GPIO_OUT_LOW)
GPIO(CHGR_OTG,             PIN(C, 3),  GPIO_OUT_LOW)

/* Inductive charging */
GPIO(CHARGE_EN,            PIN(D, 13), GPIO_OUT_LOW)
GPIO(BASE_CHG_VDD_EN,      PIN(E, 5),  GPIO_OUT_LOW)

/* USB-C Power and muxes control */
GPIO(USBC_CHARGE_EN_L,     PIN(A, 7),  GPIO_OUT_LOW)
GPIO(USBC_VCONN1_EN_L,     PIN(F, 10), GPIO_OUT_HIGH)
GPIO(USBC_VCONN2_EN_L,     PIN(D, 10), GPIO_OUT_HIGH)

GPIO(USBC_CC1_DEVICE_ODL,  PIN(A, 5),  GPIO_ODR_LOW)
GPIO(USBC_CC2_DEVICE_ODL,  PIN(E, 14), GPIO_ODR_LOW)
GPIO(USBC_CC_PUEN1,        PIN(D, 0),  GPIO_INPUT)
GPIO(USBC_CC_PUEN2,        PIN(C, 8),  GPIO_INPUT)

/* Pericom PI3USB30592 mux controls on Proto 5+ */
GPIO(USBC_MUX_CONF0,       PIN(D, 3),  GPIO_OUT_LOW)
GPIO(USBC_MUX_CONF1,       PIN(D, 9),  GPIO_OUT_LOW)
GPIO(USBC_MUX_CONF2,       PIN(E, 0),  GPIO_OUT_LOW)

GPIO(USBC_DP_HPD,          PIN(C, 1),  GPIO_ODR_LOW)

/* Inputs */
GPIO(BOARD_ID0,            PIN(E, 11),  GPIO_INPUT)
GPIO(BOARD_ID1,            PIN(E, 12),  GPIO_INPUT)
GPIO(SH_SIGNAL,            PIN(E, 2),   GPIO_INPUT)

/* Lightbar reset */
GPIO(LB_RST_L,             PIN(D, 15),  GPIO_ODR_HIGH | GPIO_PULL_UP)

#if 0
/* Alternate functions */
GPIO(USB_DM,               PIN(A, 11), GPIO_ANALOG)
GPIO(USB_DP,               PIN(A, 12), GPIO_ANALOG)
GPIO(UART_TX,              PIN(D, 5),  GPIO_OUT_LOW)
GPIO(UART_RX,              PIN(D, 6),  GPIO_OUT_LOW)
#endif

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       PIN(A, 15), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,       PIN(A, 14), GPIO_INPUT)
GPIO(SLAVE_I2C_SCL,        PIN(A, 9),  GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,        PIN(A, 10), GPIO_INPUT)

/* SCL gating for PI3USB9281 */
GPIO(PERICOM_CLK_EN,       PIN(C, 15), GPIO_OUT_HIGH)

/* Case closed debugging. */
GPIO(USB_PU_EN_L,          PIN(C, 2),  GPIO_OUT_HIGH)
GPIO(PD_DISABLE_DEBUG,     PIN(C, 6),  GPIO_OUT_LOW)
GPIO(SPI_FLASH_NSS,        PIN(B, 9),  GPIO_INPUT)
GPIO(VDDSPI_EN_0,          PIN(C, 15), GPIO_OUT_LOW) /* on rev v0+ */
GPIO(VDDSPI_EN_OLD,        PIN(C, 12), GPIO_OUT_LOW) /* on rev v6/7/8 */
GPIO(SH_RESET,             PIN(C, 4),  GPIO_ODR_HIGH)
GPIO(SH_BOOT,              PIN(C, 9),  GPIO_ODR_HIGH)
GPIO(EC_INT_L,             PIN(F, 2),  GPIO_ODR_HIGH)
GPIO(ENTERING_RW,          PIN(E, 9),  GPIO_OUT_LOW)
GPIO(WP_L,                 PIN(F, 6),  GPIO_INPUT)
GPIO(FW_DEBUG_MODE_L,      PIN(D, 1),  GPIO_ODR_HIGH)
GPIO(EC_BOOT_SPI_EN,       PIN(F, 4),  GPIO_ODR_HIGH)

#if 0
/* Alternate functions */
GPIO(AP_UART_TX,           PIN(B, 6),  GPIO_OUT_LOW)
GPIO(AP_UART_RX,           PIN(B, 7),  GPIO_INPUT)
#endif

UNIMPLEMENTED(AP_RESET_L)

ALTERNATE(PIN_MASK(C, 0x1C00), 6, MODULE_SPI_MASTER,       0)  /* SPI3: PC10/PC11/PC12 */
ALTERNATE(PIN_MASK(B, 0xC400), 5, MODULE_SPI_FLASH,        0)  /* SPI2: PB10/14/15 */
ALTERNATE(PIN_MASK(B, 0x0008), 5, MODULE_USB_PD,           0)  /* SPI1: SCK(PB3) */
ALTERNATE(PIN_MASK(B, 0x0002), 2, MODULE_USB_PD,           0)  /* TIM3_CH4: PB1 */
ALTERNATE(PIN_MASK(B, 0x00C0), 7, MODULE_USART,            0)  /* USART1: PB6/PB7 */
ALTERNATE(PIN_MASK(D, 0x0060), 7, MODULE_UART,  GPIO_PULL_UP)  /* USART2: PD4/PD5 */
ALTERNATE(PIN_MASK(A, 0xC600), 4, MODULE_I2C,              0)  /* I2C SLAVE:PA9/10 MASTER:PA14/15 */
ALTERNATE(PIN_MASK(A, 0x1800),14, MODULE_USB,              0)  /* USB: PA11/12 */
