/* -*- mode:c -*-
 *
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(POWER_BUTTON_L, PIN(E, 4), GPIO_INT_BOTH | GPIO_PULL_UP,   power_button_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI
GPIO_INT(PCH_PLTRST_L,   PIN(E, 3), GPIO_INT_BOTH | GPIO_PULL_UP,   lpcrst_interrupt)
#endif
GPIO_INT(LID_OPEN,       PIN(E, 2), GPIO_INT_BOTH | GPIO_PULL_DOWN, lid_interrupt)
GPIO_INT(WP_L,           PIN(E, 1), GPIO_INT_BOTH,                  switch_interrupt)         /* Write protect input */
#ifdef CONFIG_LOW_POWER_IDLE
GPIO_INT(UART1_RX,       PIN(B, 0), GPIO_INT_FALLING,               uart_deepsleep_interrupt) /* UART1 RX input */
#endif

#if IT83XX_PD_EVB
GPIO(USBPD_HVLDO,             PIN(A, 1), GPIO_OUT_LOW)
GPIO(USBPD_PORTB_CC1_VCONN,   PIN(A, 2), GPIO_OUT_LOW)
GPIO(USBPD_PORTB_CC2_VCONN,   PIN(A, 3), GPIO_OUT_LOW)
GPIO(USBPD_PORTB_VBUS_INPUT,  PIN(A, 4), GPIO_OUT_HIGH)
GPIO(USBPD_PORTB_VBUS_OUTPUT, PIN(A, 5), GPIO_OUT_LOW)
GPIO(USBPD_PORTB_VBUS_DROP,   PIN(A, 6), GPIO_OUT_LOW)
GPIO(USBPD_PORTA_CC1_VCONN,   PIN(C, 4), GPIO_OUT_LOW)
GPIO(USBPD_PORTA_CC2_VCONN,   PIN(C, 6), GPIO_OUT_LOW)
GPIO(USBPD_PORTA_VBUS_INPUT,  PIN(D, 0), GPIO_OUT_HIGH)
GPIO(USBPD_PORTA_VBUS_OUTPUT, PIN(D, 1), GPIO_OUT_LOW)
GPIO(USBPD_PORTA_VBUS_DROP,   PIN(E, 5), GPIO_OUT_LOW)
#endif

GPIO(PCH_SMI_L,      PIN(D, 3), GPIO_OUT_HIGH)
GPIO(PCH_SCI_L,      PIN(D, 4), GPIO_OUT_HIGH)
GPIO(GATE_A20_H,     PIN(B, 5), GPIO_OUT_HIGH)
GPIO(SYS_RESET_L,    PIN(B, 6), GPIO_OUT_HIGH)
GPIO(LPC_CLKRUN_L,   PIN(H, 0), GPIO_OUT_LOW)
GPIO(PCH_WAKE_L,     PIN(B, 7), GPIO_ODR_HIGH) /* Wake signal from EC to PCH */

GPIO(I2C_A_SCL,      PIN(B, 3), GPIO_INPUT)
GPIO(I2C_A_SDA,      PIN(B, 4), GPIO_INPUT)
GPIO(I2C_B_SCL,      PIN(C, 1), GPIO_INPUT)
GPIO(I2C_B_SDA,      PIN(C, 2), GPIO_INPUT)
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
GPIO(I2C_C_SCL,      PIN(C, 7), GPIO_INPUT)
#else
GPIO(I2C_C_SCL,      PIN(F, 6), GPIO_INPUT)
#endif
GPIO(I2C_C_SDA,      PIN(F, 7), GPIO_INPUT)

GPIO(I2C_E_SCL,      PIN(E, 0), GPIO_INPUT)
GPIO(I2C_E_SDA,      PIN(E, 7), GPIO_INPUT)

#ifdef CONFIG_UART_HOST
GPIO(UART2_SIN1,     PIN(H, 1), GPIO_INPUT)
GPIO(UART2_SOUT1,    PIN(H, 2), GPIO_INPUT)
#endif

/* KSO/KSI pins can be used as GPIO input. */
GPIO(BOARD_VERSION1, PIN(KSO_H, 5), GPIO_INPUT)
GPIO(BOARD_VERSION2, PIN(KSO_H, 6), GPIO_INPUT)
GPIO(BOARD_VERSION3, PIN(KSO_H, 7), GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, GPIO_PULL_UP) /* UART1 */
#ifdef CONFIG_UART_HOST
ALTERNATE(PIN_MASK(H, 0x06), 1, MODULE_UART, 0)            /* UART2 */
#endif
ALTERNATE(PIN_MASK(A, 0x40), 3, MODULE_SPI, 0)             /* SSCK of SPI */
ALTERNATE(PIN_MASK(C, 0x28), 3, MODULE_SPI, 0)             /* SMOSI/SMISO of SPI */
ALTERNATE(PIN_MASK(G, 0x01), 3, MODULE_SPI, 0)             /* SSCE1# of SPI */
ALTERNATE(PIN_MASK(G, 0x04), 3, MODULE_SPI, 0)             /* SSCE0# of SPI */
ALTERNATE(PIN_MASK(A, 0x80), 1, MODULE_PWM, 0)             /* PWM7 for FAN1 */
ALTERNATE(PIN_MASK(D, 0x40), 3, MODULE_PWM, 0)             /* TACH0A for FAN1 */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0)             /* I2C A SCL/SDA */
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
ALTERNATE(PIN_MASK(C, 0x86), 1, MODULE_I2C, 0)             /* I2C B SCL/SDA, C SCL */
ALTERNATE(PIN_MASK(F, 0x80), 1, MODULE_I2C, 0)             /* I2C C SDA */
#else
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, 0)             /* I2C B SCL/SDA */
ALTERNATE(PIN_MASK(F, 0xC0), 1, MODULE_I2C, 0)             /* I2C C SCL/SDA */
#endif
ALTERNATE(PIN_MASK(E, 0x81), 1, MODULE_I2C, 0)             /* I2C E SCL/SDA E0/E7 */
