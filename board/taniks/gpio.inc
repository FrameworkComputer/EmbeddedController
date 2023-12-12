/* -*- mode:c -*-
 *
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define MODULE_KB		MODULE_KEYBOARD_SCAN

/* INTERRUPT GPIOs: */
GPIO_INT(ACOK_OD,                        PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)
GPIO_INT(EC_PROCHOT_IN_L,                PIN(2, 4), GPIO_INT_BOTH, throttle_ap_prochot_input_interrupt)
GPIO_INT(EC_WP_ODL,                      PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(GSC_EC_PWR_BTN_ODL,             PIN(0, 1), GPIO_INT_BOTH | GPIO_HIB_WAKE_LOW, power_button_interrupt)
GPIO_INT(LID_OPEN,                       PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(SEQ_EC_ALL_SYS_PG,              PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SEQ_EC_DSW_PWROK,               PIN(2, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SEQ_EC_RSMRST_ODL,              PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S3_L,                       PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_SUS_L,                      PIN(2, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SYS_SLP_S0IX_L,                 PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(USB_C0_BC12_INT_ODL,            PIN(C, 6), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C0_TCPC_INT_ODL,            PIN(2, 3), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C0_PPC_INT_ODL,             PIN(6, 2), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C1_BC12_INT_ODL,            PIN(5, 0), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C1_TCPC_INT_ODL,            PIN(A, 2), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C1_PPC_INT_ODL,             PIN(F, 5), GPIO_INT_FALLING, ppc_interrupt)

/* Keyboard interrupts */
#define GPIO_KB_INPUT                    (GPIO_INPUT | GPIO_INT_RISING)

GPIO_INT(EC_KSI_00,                      PIN(4, 4), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_01,                      PIN(4, 3), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_02,                      PIN(4, 1), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_03,                      PIN(3, 7), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_04,                      PIN(F, 1), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_05,                      PIN(F, 0), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_06,                      PIN(E, 0), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_KSI_07,                      PIN(C, 7), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(EC_RFR_KEY_ODL_R,               PIN(8, 3), GPIO_INT_BOTH, keyboard_raw_gpio_interrupt)

/* USED GPIOs: */
GPIO(CCD_MODE_ODL,                   PIN(E, 5), GPIO_INPUT)
GPIO(CHARGER_VAP_OTG_EN,             PIN(7, 3), GPIO_OUT_LOW)
GPIO(CPU_C10_GATE_L,                 PIN(6, 7), GPIO_INPUT)
GPIO(EC_ACCEL_INT_R_L,               PIN(8, 1), GPIO_INPUT)
GPIO(EC_BATT_PRES_ODL,               PIN(A, 3), GPIO_INPUT)
GPIO(EC_ENTERING_RW,                 PIN(0, 3), GPIO_OUT_LOW)
GPIO(EC_EN_EDP_BL,                   PIN(D, 3), GPIO_OUT_HIGH)
GPIO(EC_GSC_PACKET_MODE,             PIN(7, 5), GPIO_OUT_LOW)
GPIO(EC_I2C_BAT_SCL,                 PIN(3, 3), GPIO_INPUT)
GPIO(EC_I2C_BAT_SDA,                 PIN(3, 6), GPIO_INPUT)
GPIO(EC_I2C_MISC_SCL_R,              PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C_MISC_SDA_R,              PIN(B, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_PPC_BC_SCL,       PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_PPC_BC_SDA,       PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C_KBMCU_SCL,               PIN(D, 1), GPIO_INPUT)
GPIO(EC_I2C_KBMCU_SDA,               PIN(D, 0), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_TCPC_SCL,         PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_TCPC_SDA,         PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_MIX_SCL,          PIN(E, 4), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_MIX_SDA,          PIN(E, 3), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_TCPC_SCL,         PIN(F, 3), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_TCPC_SDA,         PIN(F, 2), GPIO_INPUT)
GPIO(EC_KB_BL_EN_L,                  PIN(8, 6), GPIO_OUT_HIGH)
GPIO(EC_PCHHOT_ODL,                  PIN(7, 4), GPIO_INPUT)
GPIO(EC_PCH_INT_ODL,                 PIN(B, 0), GPIO_ODR_HIGH)
GPIO(EC_PCH_PWR_BTN_ODL,             PIN(C, 1), GPIO_ODR_HIGH)
GPIO(EC_PCH_RSMRST_L,                PIN(A, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_RTCRST,                  PIN(7, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_SYS_PWROK,               PIN(2, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_WAKE_R_ODL,              PIN(C, 0), GPIO_ODR_HIGH)
GPIO(EC_PROCHOT_ODL,                 PIN(6, 3), GPIO_ODR_HIGH)
GPIO(EN_PP5000_FAN,                  PIN(6, 1), GPIO_OUT_LOW)
GPIO(EN_PP5000_USBA_R,               PIN(D, 7), GPIO_OUT_LOW)
GPIO(EN_S5_RAILS,                    PIN(B, 6), GPIO_OUT_LOW)
GPIO(IMVP9_VRRDY_OD,                 PIN(3, 0), GPIO_INPUT)
GPIO(PCH_PWROK,                      PIN(7, 2), GPIO_OUT_LOW)
GPIO(SYS_RST_ODL,                    PIN(C, 5), GPIO_ODR_HIGH)
GPIO(USB_C0_TCPC_RST_ODL,            PIN(A, 7), GPIO_ODR_LOW)
GPIO(USB_C1_FRS_EN,                  PIN(9, 4), GPIO_OUT_LOW)
GPIO(USB_C1_RT_RST_R_ODL,            PIN(0, 2), GPIO_ODR_HIGH)
GPIO(VCCST_PWRGD_OD,                 PIN(A, 4), GPIO_ODR_LOW)
GPIO(KBMCU_INT_ODL,                  PIN(7, 0), GPIO_ODR_HIGH)
GPIO(EMMC_SKU_DET,                   PIN(D, 4), GPIO_INPUT)

/* KB Outputs */
#define GPIO_KB_OUTPUT               (GPIO_OUT_HIGH)

GPIO(EC_KSO_00,                      PIN(2, 1),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_01,                      PIN(2, 0),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_02_R,                    PIN(1, 7),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_03,                      PIN(1, 6),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_04,                      PIN(1, 5),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_05,                      PIN(1, 4),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_06_R,                    PIN(9, 6),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_07_R,                    PIN(A, 0),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_08,                      PIN(1, 1),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_09,                      PIN(1, 0),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_10,                      PIN(0, 7),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_11,                      PIN(0, 6),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_12,                      PIN(0, 5),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_13,                      PIN(0, 4),  GPIO_KB_OUTPUT)
GPIO(EC_KSO_14,                      PIN(8, 2),  GPIO_KB_OUTPUT)

/* UART alternate functions */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)              /* GPIO64/CR_SIN1, GPO65/CR_SOUT1/FLPRG1_L */

/* I2C alternate functions */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)               /* GPIO33/I2C5_SCL0/CTS_L, GPIO36/RTS_L/I2C5_SDA0 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)               /* GPIO87/I2C1_SDA0 */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0)               /* GPIO92/I2C2_SCL0, GPIO91/I2C2_SDA0, GPIO90/I2C1_SCL0 */
ALTERNATE(PIN_MASK(B, 0x0c), 0, MODULE_I2C, 0)               /* GPIOB3/I2C7_SCL0/DCD_L, GPIOB2/I2C7_SDA0/DSR_L */
ALTERNATE(PIN_MASK(D, 0x03), 0, MODULE_I2C, 0)               /* GPIOD1/I2C3_SCL0, GPIOD0/I2C3_SDA0 */
ALTERNATE(PIN_MASK(E, 0x18), 0, MODULE_I2C, 0)               /* GPIOE4/I2C6_SCL1/I3C_SCL, GPIOE3/I2C6_SDA1/I3C_SDA */
ALTERNATE(PIN_MASK(F, 0x0c), 0, MODULE_I2C, 0)               /* GPIOF3/I2C4_SCL1, GPIOF2/I2C4_SDA1 */

/* PWM alternate functions */
ALTERNATE(PIN_MASK(4, 0x01), 0, MODULE_PWM, 0)               /* GPIO40/TA1 */
ALTERNATE(PIN_MASK(8, 0x01), 0, MODULE_PWM, 0)               /* GPIO80/PWM3 */
ALTERNATE(PIN_MASK(B, 0x80), 0, MODULE_PWM, 0)               /* GPIOB7/PWM5 */

/* ADC alternate functions */
ALTERNATE(PIN_MASK(3, 0x10), 0, MODULE_ADC, 0)               /* GPIO34/PS2_DAT2/ADC6 */
ALTERNATE(PIN_MASK(4, 0x24), 0, MODULE_ADC, 0)               /* GPIO42/ADC3/RI_L, GPIO45/ADC0 */
ALTERNATE(PIN_MASK(E, 0x02), 0, MODULE_ADC, 0)               /* GPIOE1/ADC7 */

/* PMU alternate functions */
ALTERNATE(PIN_MASK(0, 0x01), 0, MODULE_PMU, GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH) /* PSL_IN2_L&GPI00/GPIO00 */
ALTERNATE(PIN_MASK(0, 0x02), 0, MODULE_PMU, GPIO_INT_BOTH | GPIO_HIB_WAKE_LOW) /* GPIO01/PSL_IN3_L&GPI01 */
ALTERNATE(PIN_MASK(D, 0x04), 0, MODULE_PMU, GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH) /* PSL_IN1_L&GPID2/GPIOD2 */

/* Unused Pins */
UNUSED(PIN(B, 1))       /* KSO17/GPIOB1/CR_SIN4 */
UNUSED(PIN(B, 4))       /* GPIOB4/I2C0_SDA0 */
UNUSED(PIN(B, 5))       /* GPIOB5/I2C0_SCL0 */
UNUSED(PIN(D, 6))       /* GPOD6/CR_SOUT3/SHDF_ESPI_L */
UNUSED(PIN(3, 2))       /* GPO32/TRIS_L */
UNUSED(PIN(3, 5))       /* GPO35/CR_SOUT4/TEST_L */
UNUSED(PIN(6, 6))       /* GPIO66 */
UNUSED(PIN(5, 7))       /* GPIO57/SER_IRQ/ESPI_ALERT_L */
UNUSED(PIN(9, 5))       /* GPIO95/SPIP_MISO */
UNUSED(PIN(5, 6))       /* GPIO56/CLKRUN# */
UNUSED(PIN(9, 3))       /* GPIO93/TA1/F_DIO2 */
UNUSED(PIN(9, 7))       /* GPIO97 */

/* Pre-configured PSL balls:  J8  K6 */

/*
 * The NPCX keyboard driver does not use named GPIOs to access
 * keyboard scan pins, so we do not list them in *gpio.inc. However, when
 * KEYBOARD_COL2_INVERTED is defined, this name is required.
 */

/* GPIO02_P2 to PU */
/* GPIO03_P2 to PU */
IOEX(USB_C0_OC_ODL,          EXPIN(IOEX_C0_NCT38XX, 0, 4), GPIO_ODR_HIGH)
IOEX(USB_C0_FRS_EN,          EXPIN(IOEX_C0_NCT38XX, 0, 6), GPIO_OUT_LOW)

/* LED */
GPIO(LED_1_L,	 PIN(C, 4), GPIO_OUT_HIGH)	/* Battery - Red LED */
GPIO(LED_2_L,	 PIN(C, 3), GPIO_OUT_HIGH)	/* Battery - Green LED */
GPIO(LED_3_L,	 PIN(C, 2), GPIO_OUT_HIGH)	/* Power - White LED */
