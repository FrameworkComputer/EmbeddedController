/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(UART_DBG_TX_H1_RX,		                PIN(A, 0), GPIO_INPUT)
GPIO(UART_H1_TX_DBG_RX,		                PIN(A, 1), GPIO_INPUT)
GPIO(EN_MISO_MOSI_H1_UART,		        PIN(A, 2), GPIO_OUT_LOW)
GPIO(SPIVREF_RSVD_H1VREF_H1_RST_ODL,		PIN(A, 3), GPIO_INPUT)
GPIO(SPIVREF_HOLDN_ECVREF_H1_PWRBTN_ODL,	PIN(A, 4), GPIO_INPUT)
GPIO(EN_CLK_CSN_EC_UART,	                PIN(A, 7), GPIO_OUT_LOW)

GPIO(EN_SPIVREF_RSVD_H1VREF_H1_RST,		PIN(B, 2), GPIO_OUT_LOW)
GPIO(EN_SPIVREF_HOLDN_ECVREF_H1_PWRBTN,		PIN(B, 3), GPIO_OUT_LOW)
GPIO(SEL_SPIVREF_H1VREF_3V3,		        PIN(B, 4), GPIO_OUT_LOW)
GPIO(SEL_SPIVREF_ECVREF_3V3,		        PIN(B, 5), GPIO_OUT_LOW)
GPIO(UART_DBG_TX_EC_RX_SCL,	                PIN(B, 6), GPIO_INPUT)
GPIO(UART_EC_TX_DBG_RX_SDA,	                PIN(B, 7), GPIO_INPUT)
/* Start C2D2 in UART mode */
GPIO(C2D2_MUX_UART_ODL,	                        PIN(B, 8), GPIO_ODR_LOW)

/* I2C pins should be configured as inputs until I2C module is    */
/* initialized. This will avoid driving the lines unintentionally.*/
GPIO(UART_DBG_TX_AP_RX_INA_SCL,	                PIN(B, 10), GPIO_INPUT)
GPIO(UART_AP_TX_DBG_RX_INA_SDA,	                PIN(B, 11), GPIO_INPUT)

/* Flash SPI interface */
GPIO(SPI_CSN,			                PIN(B, 12), GPIO_INPUT)
GPIO(SPI_CLK,			                PIN(B, 13), GPIO_INPUT)
GPIO(SPI_MISO,			                PIN(B, 14), GPIO_INPUT)
GPIO(SPI_MOSI,			                PIN(B, 15), GPIO_INPUT)

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

/* Default alternate mode pins */
ALTERNATE(PIN_MASK(A, GENMASK(15, 14)), 1, MODULE_UART, 0)  /* USART2: PA14/PA15 - Servo stm32 console UART*/

/* TIM16_OCN/TIM17_OCN: PB6/PB7 - Timer bit-banging for EC I2C lines */
ALTERNATE(PIN_MASK(B, GENMASK( 7,  6)), 2, MODULE_I2C_TIMERS, 0)

ALTERNATE(PIN_MASK(B, GENMASK( 7,  6)), 1, MODULE_I2C, 0)   /* I2C1: PB6/PB7 - I2C1: SCL/SDA (EC) */
ALTERNATE(PIN_MASK(B, GENMASK(11, 10)), 1, MODULE_I2C, 0)   /* I2C2: PB10/PB11 - I2C2: SCL/SDA (AP) */

ALTERNATE(PIN_MASK(B, GENMASK( 7,  6)), 0, MODULE_USART, 0) /* USART1: PB6/PB7 - Servo UART1 (EC) */
ALTERNATE(PIN_MASK(B, GENMASK(11, 10)), 4, MODULE_USART, 0) /* USART3: PB10/PB11 - Servo UART2 (AP) */
ALTERNATE(PIN_MASK(A, GENMASK( 1,  0)), 4, MODULE_USART, 0) /* USART4: PA0/PA1 - Servo UART3 (H1) */

/*
 * Note BIT(12) is intentionally not marked for alternate mode since it is
 * directly controlled with gpio_set_level and configured in the spi driver.
 */
ALTERNATE(PIN_MASK(B, GENMASK(15, 13)), 0, MODULE_SPI_FLASH, 0) /* SPI: PB15 - PB12 MOSI, MISO, CLK, CS */
