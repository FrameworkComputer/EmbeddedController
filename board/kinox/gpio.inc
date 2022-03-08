/* -*- mode:c -*-
 *
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* INTERRUPT GPIOs: */
GPIO_INT(ACOK_OD,                    PIN(0, 0), GPIO_INT_BOTH, extpower_interrupt)
GPIO_INT(EC_PROCHOT_IN_L,            PIN(F, 0), GPIO_INT_BOTH, throttle_ap_prochot_input_interrupt)
GPIO_INT(EC_WP_ODL,                  PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(GSC_EC_PWR_BTN_ODL,         PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(SEQ_EC_ALL_SYS_PG,          PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SEQ_EC_DSW_PWROK,           PIN(C, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SEQ_EC_RSMRST_ODL,          PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S3_L,                   PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_SUS_L,                  PIN(F, 1), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SYS_SLP_S0IX_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(USB_C0_BC12_INT_ODL,        PIN(C, 6), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C0_PPC_INT_ODL,         PIN(F, 5), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C0_TCPC_INT_ODL,        PIN(A, 2), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(BJ_ADP_PRESENT_ODL,         PIN(8, 2), GPIO_INT_BOTH | GPIO_PULL_UP, adp_connect_interrupt)
GPIO_INT(EC_RECOVERY_BTN_OD,         PIN(2, 3), GPIO_INT_BOTH, button_interrupt)

/* CCD */
GPIO(CCD_MODE_ODL,                   PIN(E, 5), GPIO_INPUT)

/* Security */
GPIO(EC_ENTERING_RW,                 PIN(0, 3), GPIO_OUT_LOW)
GPIO(EC_GSC_PACKET_MODE,             PIN(7, 5), GPIO_OUT_LOW)

/* Fan */
GPIO(EN_PP12000_FAN,                 PIN(6, 1), GPIO_OUT_HIGH)

/* Display */
GPIO(DP_CONN_OC_ODL,                 PIN(2, 5), GPIO_INPUT)
GPIO(HDMI_CONN_OC_ODL,               PIN(2, 4), GPIO_INPUT)

/* BarrelJack */
GPIO(EN_PPVAR_BJ_ADP_L,              PIN(0, 7), GPIO_OUT_LOW)

/* Chipset PCH */
GPIO(EC_PCHHOT_ODL,                  PIN(7, 4), GPIO_INPUT)
GPIO(EC_PCH_INT_ODL,                 PIN(B, 0), GPIO_ODR_HIGH)
GPIO(EC_PCH_RSMRST_L,                PIN(A, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_RTCRST,                  PIN(7, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_SYS_PWROK,               PIN(3, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_WAKE_ODL,                PIN(C, 0), GPIO_ODR_HIGH)
GPIO(EC_PROCHOT_ODL,                 PIN(6, 3), GPIO_ODR_HIGH)
GPIO(EN_S5_RAILS,                    PIN(B, 6), GPIO_OUT_LOW)
GPIO(PCH_PWROK,                      PIN(7, 2), GPIO_OUT_LOW)
GPIO(SYS_RST_ODL,                    PIN(C, 5), GPIO_ODR_HIGH)
GPIO(VCCST_PWRGD_OD,                 PIN(A, 4), GPIO_ODR_LOW)
GPIO(IMVP9_VRRDY_OD,                 PIN(6, 0), GPIO_INPUT)
GPIO(CPU_C10_GATE_L,                 PIN(6, 7), GPIO_INPUT)

/* Button */
GPIO(EC_PCH_PWR_BTN_ODL,             PIN(C, 1), GPIO_ODR_HIGH)
GPIO(GSC_EC_RECOVERY_BTN_OD,         PIN(2, 2), GPIO_INPUT)

GPIO(SIO_LEGO_EN,                    PIN(9, 6), GPIO_INPUT | GPIO_PULL_UP)

/* HDMI CEC */
/* TODO(b/197474873): Enable HDMI CEC */
GPIO(HDMI_CEC_IN,                    PIN(4, 0), GPIO_INPUT)
GPIO(HDMI_CEC_OUT,                   PIN(D, 3), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)
GPIO(HDMI_CEC_PULL_UP,               PIN(C, 2), GPIO_OUT_HIGH)

/* I2C SCL/SDA */
GPIO(EC_I2C_MISC_SCL_R,              PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C_MISC_SDA_R,              PIN(B, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_C2_PPC_SCL,       PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_C2_PPC_SDA,       PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_MIX_SCL,          PIN(E, 4), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_MIX_SDA,          PIN(E, 3), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_TCPC_SCL,         PIN(F, 3), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_TCPC_SDA,         PIN(F, 2), GPIO_INPUT)

/* USBA */
GPIO(EN_PP5000_USBA,                 PIN(D, 7), GPIO_OUT_LOW)
GPIO(USB_A0_OC_ODL,                  PIN(3, 1), GPIO_INPUT | GPIO_PULL_UP)
GPIO(USB_A1_OC_ODL,                  PIN(3, 0), GPIO_INPUT | GPIO_PULL_UP)
GPIO(USB_A2_OC_ODL,                  PIN(2, 7), GPIO_INPUT | GPIO_PULL_UP)
GPIO(USB_HUB_A1_OC_ODL,              PIN(2, 6), GPIO_INPUT)
GPIO(USB_HUB_A2_OC_ODL,              PIN(2, 1), GPIO_INPUT)
GPIO(USB_HUB_A3_OC_ODL,              PIN(2, 0), GPIO_INPUT)
GPIO(BTB1_USB_DET_L,                 PIN(1, 5), GPIO_INPUT)
GPIO(BTB1_DP_DET_L,                  PIN(1, 4), GPIO_INPUT)
GPIO(BTB1_COM_DET_L,                 PIN(1, 1), GPIO_INPUT)
GPIO(USB_A_OC_SOC_L,                 PIN(8, 0), GPIO_OUT_HIGH)

/* LED */
/* TODO(b/197471359): LED implementation */
GPIO(LED_PWR_L,                      PIN(C, 3), GPIO_OUT_LOW)
GPIO(LED_HDD_L,                      PIN(C, 4), GPIO_OUT_LOW)

/* USBC */
GPIO(USB_C0_FRS_EN,                  PIN(9, 4), GPIO_OUT_LOW)
GPIO(USB_C0_RT_RST_R_ODL,            PIN(0, 2), GPIO_OUT_LOW)

/* UART alternate functions */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)              /* GPIO64/CR_SIN1, GPO65/CR_SOUT1/FLPRG1_L */

/* I2C alternate functions */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)               /* GPIO33/I2C5_SCL0/CTS_L, GPIO36/RTS_L/I2C5_SDA0 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)               /* GPIO87/I2C1_SDA0 */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0)               /* GPIO92/I2C2_SCL0, GPIO91/I2C2_SDA0, GPIO90/I2C1_SCL0 */
ALTERNATE(PIN_MASK(B, 0x0c), 0, MODULE_I2C, 0)               /* GPIOB3/I2C7_SCL0/DCD_L, GPIOB2/I2C7_SDA0/DSR_L */
ALTERNATE(PIN_MASK(B, 0x30), 0, MODULE_I2C, 0)               /* GPIOB5/I2C0_SCL0, GPIOB4/I2C0_SDA0 */
ALTERNATE(PIN_MASK(D, 0x03), 0, MODULE_I2C, 0)               /* GPIOD1/I2C3_SCL0, GPIOD0/I2C3_SDA0 */
ALTERNATE(PIN_MASK(E, 0x18), 0, MODULE_I2C, 0)               /* GPIOE4/I2C6_SCL1/I3C_SCL, GPIOE3/I2C6_SDA1/I3C_SDA */
ALTERNATE(PIN_MASK(F, 0x0c), 0, MODULE_I2C, 0)               /* GPIOF3/I2C4_SCL1, GPIOF2/I2C4_SDA1 */

/* PWM alternate functions */
ALTERNATE(PIN_MASK(7, 0x08), 0, MODULE_PWM, 0)               /* GPIO73/TA2 */
ALTERNATE(PIN_MASK(B, 0x80), 0, MODULE_PWM, 0)               /* GPIOB7/PWM5 */
ALTERNATE(PIN_MASK(C, 0x18), 0, MODULE_PWM, 0)               /* GPIOC4/PWM2, GPIOC3/PWM0 */

/* ADC alternate functions */
ALTERNATE(PIN_MASK(3, 0x10), 0, MODULE_ADC, 0)               /* GPIO34/PS2_DAT2/ADC6 */
ALTERNATE(PIN_MASK(4, 0x38), 0, MODULE_ADC, 0)               /* GPIO45/ADC0, GPIO44/ADC1, GPIO43/ADC2 */
ALTERNATE(PIN_MASK(E, 0x02), 0, MODULE_ADC, 0)               /* GPIOE1/ADC7 */
ALTERNATE(PIN_MASK(4, 0x02), 0, MODULE_ADC, 0)               /* GPIO41/ADC4 */
ALTERNATE(PIN_MASK(4, 0x04), 0, MODULE_ADC, 0)               /* GPIO42/ADC3 */

/* Unused Pins */
UNUSED(PIN(D, 6))       /* GPOD6/CR_SOUT3/SHDF_ESPI_L */
UNUSED(PIN(3, 2))       /* GPO32/TRIS_L */
UNUSED(PIN(3, 5))       /* GPO35/CR_SOUT4/TEST_L */
UNUSED(PIN(6, 6))       /* GPIO66 */
UNUSED(PIN(8, 1))       /* GPIO81/PECI_DATA */
UNUSED(PIN(5, 6))       /* GPIO56/CLKRUN# */
UNUSED(PIN(9, 7))       /* GPIO97 */
UNUSED(PIN(8, 6))       /* GPIO86/TXD/CR_SOUT2 */
UNUSED(PIN(1, 3))       /* KSO06/GPO13/GP_SEL# */
UNUSED(PIN(1, 2))       /* KSO07/GPO12/JEN# */
UNUSED(PIN(0, 6))       /* KSO11/GPIO06/P80_CLK */
UNUSED(PIN(0, 4))       /* KSO13/GPIO04 */
UNUSED(PIN(0, 5))       /* KSO12/GPIO05 */
UNUSED(PIN(1, 0))       /* KSO09/GPIO10 */
UNUSED(PIN(1, 6))       /* KSO03/GPIO16 */
UNUSED(PIN(1, 7))       /* KSO02/GPIO17 */
UNUSED(PIN(3, 3))       /* GPIO33 */
UNUSED(PIN(3, 6))       /* GPIO36 */
UNUSED(PIN(E, 0))       /* GPIOE0/ADC10 */
UNUSED(PIN(5, 0))       /* GPIO50 */
UNUSED(PIN(6, 2))       /* GPIO62/PS2_CLK1 */
UNUSED(PIN(7, 0))       /* GPIO70/PS2_DAT0 */
UNUSED(PIN(8, 3))       /* KSO15/GPIO83 */
UNUSED(PIN(8, 7))       /* GPIO87/I2C1_SDA0 */
UNUSED(PIN(9, 0))       /* GPIO90/I2C1_SCL0 */
UNUSED(PIN(9, 3))       /* GPIO93 */
UNUSED(PIN(9, 5))       /* GPIO95/SPIP_MISO */
UNUSED(PIN(A, 0))       /* F_CS0#/GPIOA0 */
UNUSED(PIN(A, 3))       /* GPIOA3/SPIP_MOSI */
UNUSED(PIN(A, 7))       /* GPIOA7/PS2_DAT3 */
UNUSED(PIN(B, 1))       /* KSO17/GPIOB1 */
UNUSED(PIN(B, 4))       /* GPIOB4/I2C0_SDA0 */
UNUSED(PIN(B, 5))       /* GPIOB5/I2C0_SCL0 */
UNUSED(PIN(D, 0))       /* GPIOD0/I2C3_SDA0 */
UNUSED(PIN(D, 1))       /* GPIOD1/I2C3_SCL0 */
UNUSED(PIN(D, 2))       /* PSL_IN1#/GPIOD2 */
UNUSED(PIN(D, 4))       /* GPIOD4/CR_SIN3 */