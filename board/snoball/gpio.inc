/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* TCPC alert / interrupt inputs */
GPIO_INT(TCPC1_INT,	PIN(A, 11),	GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(TCPC2_INT,	PIN(A, 12),	GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(TCPC3_INT,	PIN(A, 13),	GPIO_INT_FALLING, tcpc_alert_event)

/* ADCs */
GPIO(PD1_CURRENT_SENSE,	PIN(A, 0),	GPIO_ANALOG)
GPIO(PD2_CURRENT_SENSE,	PIN(A, 1),	GPIO_ANALOG)
GPIO(PD3_CURRENT_SENSE,	PIN(A, 2),	GPIO_ANALOG)
GPIO(PD1_VOLTAGE_SENSE,	PIN(A, 3),	GPIO_ANALOG)
GPIO(PD2_VOLTAGE_SENSE,	PIN(A, 4),	GPIO_ANALOG)
GPIO(PD3_VOLTAGE_SENSE,	PIN(A, 5),	GPIO_ANALOG)
GPIO(VBUCK_IN_SENSE,	PIN(B, 0),	GPIO_ANALOG)
GPIO(TEMP_SENSE,	PIN(B, 1),	GPIO_ANALOG)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C1_SCL,		PIN(B, 8),	GPIO_INPUT)
GPIO(I2C1_SDA,		PIN(B, 9),	GPIO_INPUT)
GPIO(I2C2_SCL,		PIN(B, 13),	GPIO_INPUT)
GPIO(I2C2_SDA,		PIN(B, 11), 	GPIO_INPUT)
/* Mux select for I2C2 TCPCs */
GPIO(I2C_MUX_SEL,	PIN(B, 2), 	GPIO_OUT_LOW)

GPIO(BIAS_EN,		PIN(B, 10),	GPIO_OUT_HIGH)
/* Primary-side communication GPIOs */
GPIO(OPTO_IN,		PIN(B, 12),	GPIO_INPUT)
GPIO(OPTO_OUT,		PIN(A, 8),	GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

/* Alternate functions */
#if 0
GPIO(EC_UART_TX,	PIN(A, 9),	GPIO_OUT_LOW)
GPIO(EC_UART_RX,	PIN(A, 10),	GPIO_INPUT)
GPIO(DSPIC_UART_TX,	PIN(A, 14),	GPIO_OUT_LOW)
GPIO(DSPIC_UART_RX,	PIN(A, 15),	GPIO_INPUT)

/* WKUP6 */
GPIO(WAKE,		PIN(B, 5),	GPIO_INPUT)
#endif

/* PWM outputs w/ negation */
GPIO(PD1_PWM,		PIN(B, 14),	GPIO_OUT_LOW)
GPIO(PD1_PWM_CMP,	PIN(B, 15),	GPIO_OUT_LOW)
GPIO(PD2_PWM,		PIN(A, 6),	GPIO_OUT_LOW)
GPIO(PD2_PWM_CMP,	PIN(B, 6),	GPIO_OUT_LOW)
GPIO(PD3_PWM,		PIN(A, 7),	GPIO_OUT_LOW)
GPIO(PD3_PWM_CMP,	PIN(B, 7),	GPIO_OUT_LOW)

/* PB8 / PB9 / PB11: I2C1_SCL / I2C1_SDA / I2C2_SDA */
ALTERNATE(PIN_MASK(B, 0x0B00), 1, MODULE_I2C,      0)
/* PB13: I2C2_SCL */
ALTERNATE(PIN_MASK(B, 0x2000), 5, MODULE_I2C,      0)
/* PA9 / PA10 / PA14 / PA15: USART1 / USART2 */
ALTERNATE(PIN_MASK(A, 0xC600), 1, MODULE_UART,     0)
/* PA6 / PA7: TIM16_CH1 / TIM17_CH1 */
ALTERNATE(PIN_MASK(A, 0x00C0), 5, MODULE_PWM, 0)
/* PB6 / PB7: TIM16_CH1N / TIM17_CH1N */
ALTERNATE(PIN_MASK(B, 0x00C0), 2, MODULE_PWM, 0)
/* PB14: TIM15_CH1 */
ALTERNATE(PIN_MASK(B, 0x4000), 1, MODULE_PWM, 0)
/* PB15: TIM15_CH1N */
ALTERNATE(PIN_MASK(B, 0x8000), 3, MODULE_PWM, 0)
