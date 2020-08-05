/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Pin names follow the schematic, and are aliased to other names if necessary.
 * Note: Those with interrupt handlers must be declared first. */

/* Latency on this interrupt is extremely critical, so it comes first to ensure
 * it gets placed first in gpio_wui_table so gpio_interrupt() needs to do
 * minimal scanning. */
GPIO_INT(CPU_C10_GATE_L,       PIN(6, 7), GPIO_INT_BOTH, c10_gate_interrupt)

/* Wake Source interrupts */
GPIO_INT(EC_WP_ODL,            PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(H1_EC_PWR_BTN_ODL,    PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)

/* Power sequencing interrupts */
GPIO_INT(PG_PP5000_A_OD,       PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_PP1800_A_OD,       PIN(3, 1), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_VPRIM_CORE_A_OD,   PIN(2, 3), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_PP1050_A_OD,       PIN(2, 2), GPIO_INT_BOTH, power_signal_interrupt)
/* EC output, but also interrupt so this can be polled as a power signal */
GPIO_INT(EC_PCH_RSMRST_L,      PIN(A, 6), GPIO_OUTPUT | GPIO_INT_F_RISING | GPIO_INT_F_FALLING, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S4
GPIO_INT(SLP_S4_L,             PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
#endif
GPIO_INT(PG_PP2500_DRAM_U_OD,  PIN(2, 0), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_PP1200_U_OD,       PIN(2, 1), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
GPIO_INT(SLP_S3_L,             PIN(A, 5), GPIO_INT_BOTH, slp_s3_interrupt)
#endif
GPIO_INT(PG_PP950_VCCIO_OD,    PIN(1, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S0_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(IMVP8_VRRDY_OD,       PIN(1, 6), GPIO_INT_BOTH, power_signal_interrupt)

/* Other interrupts */
GPIO_INT(USB_C0_TCPPC_INT_ODL, PIN(E, 0), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C0_TCPC_INT_ODL,  PIN(6, 2), GPIO_INT_FALLING, tcpc_alert_event)
/*
 * Directly connected recovery button (not available on some boards).
 */
GPIO_INT(EC_RECOVERY_BTN_ODL,  PIN(F, 1), GPIO_INT_BOTH, button_interrupt)
/*
 * Recovery button input from H1.
 */
GPIO_INT(H1_EC_RECOVERY_BTN_ODL, PIN(2, 4), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(BJ_ADP_PRESENT_L,     PIN(8, 2), GPIO_INT_BOTH | GPIO_PULL_UP, adp_connect_interrupt)

/* Port power control interrupts */
GPIO_INT(HDMI_CONN0_OC_ODL,    PIN(0, 7), GPIO_INT_BOTH, port_ocp_interrupt)
GPIO_INT(HDMI_CONN1_OC_ODL,    PIN(0, 6), GPIO_INT_BOTH, port_ocp_interrupt)
GPIO_INT(USB_A0_OC_ODL,        PIN(E, 4), GPIO_INT_BOTH, port_ocp_interrupt)
GPIO_INT(USB_A1_OC_ODL,        PIN(A, 2), GPIO_INT_BOTH, port_ocp_interrupt)
GPIO_INT(USB_A2_OC_ODL,        PIN(F, 5), GPIO_INT_BOTH, port_ocp_interrupt)
GPIO_INT(USB_A3_OC_ODL,        PIN(0, 3), GPIO_INT_BOTH, port_ocp_interrupt)
/* May be reconfigured as input */
GPIO_INT(USB_A4_OC_ODL,        PIN(B, 0), GPIO_OUT_LOW | GPIO_INT_BOTH, port_ocp_interrupt)

/* PCH/CPU signals */
GPIO(EC_PCH_PWROK,             PIN(0, 5), GPIO_OUT_LOW)
GPIO(EC_PCH_SYS_PWROK,         PIN(3, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_PWR_BTN_ODL,       PIN(C, 1), GPIO_ODR_HIGH)
GPIO(EC_PCH_RTCRST,            PIN(7, 6), GPIO_ODR_HIGH)
GPIO(EC_PCH_WAKE_ODL,          PIN(7, 4), GPIO_ODR_HIGH)
GPIO(EC_PROCHOT_IN_OD,         PIN(3, 4), GPIO_INPUT)
GPIO(EC_PROCHOT_ODL,           PIN(6, 3), GPIO_ODR_HIGH)
GPIO(SYS_RST_ODL,              PIN(C, 5), GPIO_ODR_HIGH)

/* Power control outputs */
GPIO(EN_PP5000_A,              PIN(A, 4), GPIO_OUT_LOW)
GPIO(EN_PP3300_INA_H1_EC_ODL,  PIN(5, 7), GPIO_ODR_HIGH)
GPIO(EN_PP1800_A,              PIN(1, 5), GPIO_OUT_LOW)
GPIO(VCCST_PG_OD,              PIN(1, 4), GPIO_ODR_LOW)
GPIO(EN_S0_RAILS,              PIN(1, 1), GPIO_OUT_LOW)
GPIO(EN_ROA_RAILS,             PIN(A, 3), GPIO_OUT_LOW)
GPIO(EN_PP950_VCCIO,           PIN(1, 0), GPIO_OUT_LOW)
GPIO(EC_IMVP8_PE,              PIN(A, 7), GPIO_OUT_LOW)
GPIO(EN_IMVP8_VR,              PIN(F, 4), GPIO_OUT_LOW)

/* Barreljack */
GPIO(EN_PPVAR_BJ_ADP_L,        PIN(0, 4), GPIO_OUT_LOW)

/* USB type A */
GPIO(EN_PP5000_USB_VBUS,       PIN(8, 3), GPIO_OUT_LOW)
GPIO(USB_A_LOW_PWR_OD,         PIN(9, 4), GPIO_ODR_LOW)
GPIO(USB_A2_STATUS_L,          PIN(6, 1), GPIO_INPUT)
GPIO(USB_A3_STATUS_L,          PIN(C, 7), GPIO_INPUT)

/* USB type C */
GPIO(USB_C0_TCPC_RST,          PIN(9, 7), GPIO_OUT_LOW)

/* Misc. */
GPIO(M2_SSD_PLN,               PIN(A, 0), GPIO_INPUT)
GPIO(EC_ENTERING_RW,           PIN(E, 3), GPIO_OUT_LOW)
GPIO(CCD_MODE_ODL,             PIN(E, 5), GPIO_ODR_HIGH)
GPIO(EN_PP_MST_OD,             PIN(9, 6), GPIO_ODR_HIGH)
GPIO(PACKET_MODE_EN,           PIN(7, 5), GPIO_OUT_LOW)

/* HDMI/CEC */
GPIO(EN_PP5000_HDMI,           PIN(5, 0), GPIO_OUT_LOW)
GPIO(HDMI_CONN0_CEC_OUT,       PIN(B, 1), GPIO_ODR_HIGH)
GPIO(HDMI_CONN0_CEC_IN,        PIN(4, 0), GPIO_INPUT)
GPIO(HDMI_CONN1_CEC_OUT,       PIN(9, 5), GPIO_ODR_HIGH)
GPIO(HDMI_CONN1_CEC_IN,        PIN(D, 3), GPIO_INPUT)

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C0_SCL,                 PIN(B, 5), GPIO_INPUT)           /* EC_I2C_INA_SCL */
GPIO(I2C0_SDA,                 PIN(B, 4), GPIO_INPUT)           /* EC_I2C_INA_SDA */
GPIO(I2C1_SCL,                 PIN(9, 0), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPPC_SCL */
GPIO(I2C1_SDA,                 PIN(8, 7), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPPC_SDA */
GPIO(I2C3_SCL,                 PIN(D, 1), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPC_SCL */
GPIO(I2C3_SDA,                 PIN(D, 0), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPC_SDA */
GPIO(I2C5_SCL,                 PIN(3, 3), GPIO_INPUT)           /* EC_I2C_IMVP8_SCL */
GPIO(I2C5_SDA,                 PIN(3, 6), GPIO_INPUT)           /* EC_I2C_IMVP8_SDA */
GPIO(I2C7_SCL,                 PIN(B, 3), GPIO_INPUT)           /* EC_I2C_EEPROM_SCL */
GPIO(I2C7_SDA,                 PIN(B, 2), GPIO_INPUT)           /* EC_I2C_EEPROM_SDA */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x30), 0, MODULE_I2C, 0)                  /* I2C0 */
ALTERNATE(PIN_MASK(9, 0x01), 0, MODULE_I2C, 0)                  /* I2C1 SCL */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)                  /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, 0x03), 0, MODULE_I2C, 0)                  /* I2C3 */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)                  /* I2C5 */
ALTERNATE(PIN_MASK(B, 0x0C), 0, MODULE_I2C, 0)                  /* I2C7 */

/* PWM */
ALTERNATE(PIN_MASK(C, 0x08), 0, MODULE_PWM, 0)                  /* PWM0 - Red Led */
ALTERNATE(PIN_MASK(C, 0x10), 0, MODULE_PWM, 0)                  /* PWM2 - Green Led */
ALTERNATE(PIN_MASK(B, 0x80), 0, MODULE_PWM, 0)                  /* PWM5 - Fan 1 */
ALTERNATE(PIN_MASK(7, 0x08), 0, MODULE_PWM, 0)                  /* TA2 - Fan Tachometer */

/* ADC */
ALTERNATE(PIN_MASK(4, 0x2A), 0, MODULE_ADC, 0)                  /* ADC0, ADC2, ADC4 */
ALTERNATE(PIN_MASK(E, 0x02), 0, MODULE_ADC, 0)                  /* ADC7 */
ALTERNATE(PIN_MASK(F, 0x01), 0, MODULE_ADC, 0)                  /* ADC9 */

/* UART */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)                 /* UART from EC to Servo */

/* Unused pins */
UNUSED(PIN(1, 3))						/* EC_GP_SEL1_ODL */
UNUSED(PIN(F, 2))						/* EC_I2C_RFU_SDA */
UNUSED(PIN(F, 3))						/* EC_I2C_RFU_SCL */
UNUSED(PIN(C, 0))						/* FAN_PWM_2 */
UNUSED(PIN(8, 0))						/* LED_BLUE_L */
UNUSED(PIN(4, 4))						/* ADC1/TEMP_SENSOR_2 */
UNUSED(PIN(4, 2))						/* ADC3/TEMP_SENSOR_3 */
UNUSED(PIN(C, 2))						/* A12 NC */
UNUSED(PIN(9, 2))						/* K8  NC */
UNUSED(PIN(9, 1))						/* L8  NC */
UNUSED(PIN(1, 2))						/* C6  NC */
UNUSED(PIN(6, 6))						/* H4  NC */
UNUSED(PIN(8, 1))						/* L6  NC */
UNUSED(PIN(C, 6))						/* B11 NC */
UNUSED(PIN(E, 2))						/* B8  NC */
UNUSED(PIN(8, 5))						/* L7  NC */
UNUSED(PIN(0, 0))						/* D11 NC */
UNUSED(PIN(3, 2))						/* E5  NC */
UNUSED(PIN(D, 6))						/* F6  NC */
UNUSED(PIN(3, 5))						/* F5  NC */
UNUSED(PIN(5, 6))						/* M2  NC */
UNUSED(PIN(D, 2))						/* C11 NC */
UNUSED(PIN(8, 6))						/* J8  NC */
UNUSED(PIN(9, 3))						/* M11 NC */
UNUSED(PIN(7, 2))						/* H6  NC */
