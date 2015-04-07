/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
/* Keyboard power button */
GPIO(KB_PWR_ON_L,    B, 5,  GPIO_INT_BOTH,     power_signal_interrupt)
GPIO(PP1800_LDO2,    A, 1,  GPIO_INT_BOTH,     power_signal_interrupt) /* LDO2 is ON (end of PMIC sequence) */
GPIO(SOC1V8_XPSHOLD, A, 3,  GPIO_INT_BOTH,     power_signal_interrupt) /* App Processor ON  */
GPIO(CHARGER_INT_L,  C, 4,  GPIO_INT_FALLING,  pmu_irq_handler)
GPIO(LID_OPEN,       C, 13, GPIO_INT_BOTH,     lid_interrupt)          /* LID switch detection */
GPIO(SUSPEND_L,      A, 7,  INT_BOTH_FLOATING, power_signal_interrupt) /* AP suspend/resume state */

/* Keyboard inputs */
GPIO(KB_IN00,     C, 8,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN01,     C, 9,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN02,     C, 10, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN03,     C, 11, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN04,     C, 12, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN05,     C, 14, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN06,     C, 15, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(KB_IN07,     D, 2,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO(USB_CHG_INT, A, 6,  GPIO_INT_FALLING, extpower_interrupt)

GPIO(WP_L,           A, 13, GPIO_INPUT,        NULL)                   /* Write protection pin (low active) */

/* Other inputs */
GPIO(BCHGR_VACG, A, 0, GPIO_INT_BOTH, NULL) /* AC good on TPSChrome */

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C1_SCL, B, 6,  GPIO_INPUT, NULL)
GPIO(I2C1_SDA, B, 7,  GPIO_INPUT, NULL)
GPIO(I2C2_SCL, B, 10, GPIO_INPUT, NULL)
GPIO(I2C2_SDA, B, 11, GPIO_INPUT, NULL)

/* Outputs */
GPIO(EN_PP1350,   A, 14, GPIO_OUT_LOW,   NULL) /* DDR 1.35v rail enable */
GPIO(EN_PP5000,   A, 11, GPIO_OUT_LOW,   NULL) /* 5.0v rail enable */
GPIO(EN_PP3300,   A, 8,  GPIO_OUT_LOW,   NULL) /* 3.3v rail enable */
GPIO(PMIC_PWRON_L,A, 12, GPIO_OUT_HIGH,  NULL) /* 5v rail ready */
GPIO(PMIC_RESET,  A, 15, GPIO_OUT_LOW,   NULL) /* Force hard reset of the pmic */
GPIO(ENTERING_RW, D, 0,  GPIO_OUT_LOW,   NULL) /* EC is R/W mode for the kbc mux */
GPIO(CHARGER_EN,  B, 2,  GPIO_OUT_LOW,   NULL)
GPIO(EC_INT,      B, 9,  GPIO_ODR_HIGH,  NULL)
GPIO(ID_MUX,      D, 1,  GPIO_OUT_LOW,   NULL)
GPIO(KB_OUT00,    B, 0,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT01,    B, 8,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT02,    B, 12, GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT03,    B, 13, GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT04,    B, 14, GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT05,    B, 15, GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT06,    C, 0,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT07,    C, 1,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT08,    C, 2,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT09,    B, 1,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT10,    C, 5,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT11,    C, 6,  GPIO_KB_OUTPUT, NULL)
GPIO(KB_OUT12,    C, 7,  GPIO_KB_OUTPUT, NULL)
GPIO(BOOST_EN,    B, 3,  GPIO_OUT_HIGH,  NULL)
GPIO(ILIM,        B, 4,  GPIO_OUT_LOW,   NULL)
