/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(POWER_BUTTON_L, PIN(E, 4), GPIO_INT_BOTH | GPIO_PULL_UP,   power_button_interrupt)
GPIO_INT(PCH_PLTRST_L,   PIN(E, 3), GPIO_INT_BOTH | GPIO_PULL_UP,   lpcrst_interrupt)
GPIO_INT(LID_OPEN,       PIN(E, 2), GPIO_INT_BOTH | GPIO_PULL_DOWN, lid_interrupt)
GPIO_INT(WP_L,           PIN(E, 1), GPIO_INT_BOTH,                  switch_interrupt)         /* Write protect input */

GPIO(H_LED0,         PIN(A, 0), GPIO_ODR_HIGH)
GPIO(H_LED1,         PIN(A, 1), GPIO_ODR_HIGH)
GPIO(H_LED2,         PIN(A, 2), GPIO_ODR_HIGH)
GPIO(H_LED3,         PIN(A, 3), GPIO_ODR_HIGH)
GPIO(H_LED4,         PIN(A, 4), GPIO_ODR_HIGH)
GPIO(H_LED5,         PIN(A, 5), GPIO_ODR_HIGH)
GPIO(H_LED6,         PIN(A, 6), GPIO_ODR_HIGH)
GPIO(L_LED0,         PIN(I, 0), GPIO_ODR_HIGH)
GPIO(L_LED1,         PIN(I, 1), GPIO_ODR_HIGH)
GPIO(L_LED2,         PIN(I, 2), GPIO_ODR_HIGH)
GPIO(L_LED3,         PIN(I, 3), GPIO_ODR_HIGH)
GPIO(L_LED4,         PIN(I, 4), GPIO_ODR_HIGH)
GPIO(L_LED5,         PIN(I, 5), GPIO_ODR_HIGH)
GPIO(L_LED6,         PIN(I, 6), GPIO_ODR_HIGH)
GPIO(BUSY_LED,       PIN(J, 0), GPIO_OUT_LOW)
GPIO(GOOD_LED,       PIN(J, 1), GPIO_OUT_HIGH)
GPIO(FAIL_LED,       PIN(J, 2), GPIO_OUT_LOW)
GPIO(PCH_SMI_L,      PIN(D, 3), GPIO_OUT_HIGH)
GPIO(PCH_SCI_L,      PIN(D, 4), GPIO_OUT_HIGH)
GPIO(GATE_A20_H,     PIN(B, 5), GPIO_OUT_HIGH)
GPIO(PCH_RCIN_L,     PIN(B, 6), GPIO_OUT_HIGH)
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

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, GPIO_PULL_UP) /* UART0 */
ALTERNATE(PIN_MASK(A, 0x40), 3, MODULE_SPI, 0)             /* SSCK of SPI */
ALTERNATE(PIN_MASK(C, 0x28), 3, MODULE_SPI, 0)             /* SMOSI/SMISO of SPI */
ALTERNATE(PIN_MASK(G, 0x01), 3, MODULE_SPI, 0)             /* SSCE1# of SPI */
ALTERNATE(PIN_MASK(G, 0x04), 3, MODULE_SPI, 0)             /* SSCE0# of SPI */
ALTERNATE(PIN_MASK(A, 0x80), 1, MODULE_PWM_FAN, 0)         /* PWM7 for FAN1 */
ALTERNATE(PIN_MASK(D, 0x40), 3, MODULE_PWM_FAN, 0)         /* TACH0A for FAN1 */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C, 0)             /* I2C A SCL/SDA */
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
ALTERNATE(PIN_MASK(C, 0x86), 1, MODULE_I2C, 0)             /* I2C B SCL/SDA, C SCL */
ALTERNATE(PIN_MASK(F, 0x80), 1, MODULE_I2C, 0)             /* I2C C SDA */
#else
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C, 0)             /* I2C B SCL/SDA */
ALTERNATE(PIN_MASK(F, 0xC0), 1, MODULE_I2C, 0)             /* I2C C SCL/SDA */
#endif
