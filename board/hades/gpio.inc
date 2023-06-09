/* -*- mode:c -*-
 *
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define MODULE_KB		MODULE_KEYBOARD_SCAN

/* INTERRUPT GPIOs: */
GPIO_INT(SEQ_EC_DSW_PWROK,               PIN(C, 7), GPIO_INT_BOTH, intel_x86_pwrok_signal_interrupt)
GPIO_INT(ACOK_OD,                        PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)
GPIO_INT(EC_PROCHOT_IN_L,                PIN(A, 0), GPIO_INT_BOTH, throttle_ap_prochot_input_interrupt)
GPIO_INT(EC_WP_ODL,                      PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(GSC_EC_PWR_BTN_ODL,             PIN(0, 1), GPIO_INT_BOTH | GPIO_HIB_WAKE_LOW, power_button_interrupt)
GPIO_INT(LID_OPEN,                       PIN(D, 2), GPIO_INT_BOTH | GPIO_PULL_UP | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(SEQ_EC_ALL_SYS_PG,              PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SEQ_EC_RSMRST_ODL,              PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S3_L,                       PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_SUS_L,                      PIN(F, 1), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SYS_SLP_S0IX_L,                 PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(USB_C0_BC12_INT_ODL,            PIN(C, 6), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C0_TCPC_INT_ODL,            PIN(E, 0), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C0_PPC_INT_ODL,             PIN(A, 7), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C1_BC12_INT_ODL,            PIN(8, 3), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C1_TCPC_INT_ODL,            PIN(7, 0), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C1_PPC_INT_ODL,             PIN(B, 1), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(BJ_ADP_PRESENT_ODL,             PIN(9, 5), GPIO_INT_BOTH | GPIO_PULL_UP, bj_present_interrupt)
GPIO_INT(GPU_OVERT_ODL,                  PIN(5, 0), GPIO_INT_BOTH, gpu_overt_interrupt)

/* USED GPIOs: */
GPIO(CCD_MODE_ODL,                   PIN(E, 5), GPIO_INPUT)
GPIO(CPU_C10_GATE_L,                 PIN(6, 7), GPIO_INPUT)
GPIO(EC_BATT_PRES_ODL,               PIN(A, 3), GPIO_INPUT)
GPIO(EC_ENTERING_RW,                 PIN(0, 3), GPIO_OUT_LOW)
GPIO(EC_EN_EDP_BL,                   PIN(D, 3), GPIO_ODR_HIGH)
GPIO(EC_GSC_PACKET_MODE,             PIN(7, 5), GPIO_OUT_LOW)
GPIO(EC_I2C_BAT_SCL,                 PIN(3, 3), GPIO_INPUT)
GPIO(EC_I2C_BAT_SDA,                 PIN(3, 6), GPIO_INPUT)
GPIO(EC_I2C_MISC_SCL_R,              PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C_MISC_SDA_R,              PIN(B, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_SCL,              PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_SDA,              PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_SCL,              PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_SDA,              PIN(8, 7), GPIO_INPUT)
GPIO(EN_EC_KB_BL_L,                  PIN(8, 6), GPIO_ODR_HIGH)
GPIO(IMVP91_PE_EC,                   PIN(E, 3), GPIO_OUT_LOW)
GPIO(EC_PCHHOT_ODL,                  PIN(7, 4), GPIO_INPUT)
GPIO(EC_PCH_INT_ODL,                 PIN(B, 0), GPIO_ODR_HIGH)
GPIO(EC_PCH_PWR_BTN_ODL,             PIN(C, 1), GPIO_ODR_HIGH)
GPIO(EC_PCH_RSMRST_L,                PIN(A, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_RTCRST,                  PIN(7, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_SYS_PWROK,               PIN(3, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_WAKE_R_ODL,              PIN(C, 0), GPIO_ODR_HIGH)
GPIO(EC_PROCHOT_ODL,                 PIN(6, 3), GPIO_ODR_HIGH)
GPIO(EN_PP5000_FAN,                  PIN(6, 1), GPIO_OUT_LOW)
GPIO(EN_PP12000_FAN_X,               PIN(F, 5), GPIO_OUT_LOW)
GPIO(EN_PP5000_USBA_R,               PIN(D, 7), GPIO_OUT_LOW)
GPIO(EN_S5_RAILS,                    PIN(9, 6), GPIO_OUT_LOW)
GPIO(IMVP9_VRRDY_OD,                 PIN(4, 3), GPIO_INPUT)
GPIO(PCH_PWROK,                      PIN(7, 2), GPIO_OUT_LOW)
GPIO(SYS_RST_ODL,                    PIN(C, 5), GPIO_ODR_HIGH)
GPIO(EC_USB_PCH_C0_OC_ODL,           PIN(9, 4), GPIO_ODR_HIGH)
GPIO(EC_USB_PCH_C1_OC_ODL,           PIN(9, 7), GPIO_ODR_HIGH)
GPIO(VCCST_PWRGD_OD,                 PIN(A, 4), GPIO_ODR_LOW)
GPIO(EN_USB_A_LOW_POWER,             PIN(9, 3), GPIO_OUT_LOW)
GPIO(EN_USB_C1_28V,                  PIN(B, 5), GPIO_OUT_LOW)
GPIO(USB_C0_TCPC_RST_ODL,            PIN(D, 4), GPIO_OUT_HIGH)
GPIO(NVIDIA_GPU_ACOFF_ODL,           PIN(5, 6), GPIO_ODR_HIGH)
GPIO(LED_4_L,                        PIN(6, 0), GPIO_OUT_HIGH)
GPIO(LED_3_L,                        PIN(C, 2), GPIO_OUT_HIGH)
GPIO(LED_2_L,                        PIN(C, 3), GPIO_OUT_HIGH)
GPIO(LED_1_L,                        PIN(C, 4), GPIO_OUT_HIGH)
GPIO(USB_C0_PPC_HI_ILIM,             PIN(F, 2), GPIO_OUT_HIGH)
GPIO(USB_C1_PPC_HI_ILIM,             PIN(F, 3), GPIO_OUT_HIGH)
GPIO(USB_C0_IN_HPD,                  PIN(6, 2), GPIO_OUT_LOW)
/* Block AP power sequencing (b/279214842) */
GPIO(PG_PP3300_S5_OD,                PIN(B, 4), GPIO_ODR_HIGH)

/*
 * Barrel-jack adapter enable switch. When starting up on a depleted battery,
 * we'll be powered by either BJ or USB-C but not both. The EC will detect BJ
 * or USBC and disable the other ports.
 */
GPIO(EN_PPVAR_BJ_ADP_L,              PIN(A, 2), GPIO_OUT_LOW)

/*
 * The NPCX keyboard driver does not use named GPIOs to access
 * keyboard scan pins, so we do not list them in *gpio.inc. However, when
 * KEYBOARD_COL2_INVERTED is defined, this name is required.
 */
GPIO(EC_KSO_02_INV,               PIN(1, 7), GPIO_OUT_LOW)

/* UART alternate functions */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0) /* GPIO64/CR_SIN1, GPO65/CR_SOUT1/FLPRG1_L */

/* I2C alternate functions */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)  /* GPIO33/I2C5_SCL0/CTS_L, GPIO36/RTS_L/I2C5_SDA0 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)  /* GPIO87/I2C1_SDA0 */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0)  /* GPIO92/I2C2_SCL0, GPIO91/I2C2_SDA0, GPIO90/I2C1_SCL0 */
ALTERNATE(PIN_MASK(B, 0x0c), 0, MODULE_I2C, 0)  /* GPIOB3/I2C7_SCL0/DCD_L, GPIOB2/I2C7_SDA0/DSR_L */

/* PWM alternate functions */
ALTERNATE(PIN_MASK(4, 0x01), 0, MODULE_PWM, 0)  /* GPIO40/TA1 EC_FAN1_TACH */
ALTERNATE(PIN_MASK(7, 0x08), 0, MODULE_PWM, 0)  /* GPIO73/TA2 EC_FAN2_TACH */
ALTERNATE(PIN_MASK(8, 0x01), 0, MODULE_PWM, 0)  /* GPIO80/PWM3 EC_KB_BL_PWM */
ALTERNATE(PIN_MASK(B, 0xC0), 0, MODULE_PWM, 0)  /* GPIOB7/PWM5=FAN1_PWM,
                                                   GPIOB6/PWM4=FAN2_PWM */

/* ADC alternate functions */
ALTERNATE(PIN_MASK(3, 0x10), 0, MODULE_ADC, 0)  /* GPIO34/PS2_DAT2/ADC6=TEMP_SENSOR_3 */
ALTERNATE(PIN_MASK(4, 0x36), 0, MODULE_ADC, 0)  /* GPIO42/ADC3/RI_L=CHARGER_IADP_R,
                                                   GPIO45/ADC0=TEMP_SENSOR_1,
                                                   GPIO44/ADC1=TEMP_SENSOR_2,
                                                   GPIO41/ADC4=ADP_TYP. */
ALTERNATE(PIN_MASK(E, 0x02), 0, MODULE_ADC, 0)  /* GPIOE1/ADC7=TEMP_SENSOR_4 */
ALTERNATE(PIN_MASK(F, 0x01), 0, MODULE_ADC, 0)  /* GPIOF0/ADC9=USB_C1_VBUSM */

/* KB alternate functions */
ALTERNATE(PIN_MASK(0, 0xf0), 0, MODULE_KB, GPIO_ODR_HIGH)    /* KSO10&P80_CLK/GPIO07, KSO11&P80_DAT/GPIO06, KSO12/GPIO05, KSO13/GPIO04 */
ALTERNATE(PIN_MASK(1, 0x7f), 0, MODULE_KB, GPIO_ODR_HIGH)    /* KSO06/GPO13/GP_SEL_L, KSO07/GPO12/JEN_L, KSO03/GPIO16/JTAG_TDO0_SWO, KSO04/GPIO15/XNOR, KSO05/GPIO14, KSO08/GPIO11/CR_SOUT1, KSO09/GPIO10/CR_SIN1 */
ALTERNATE(PIN_MASK(2, 0xfc), 0, MODULE_KB, GPIO_INPUT | GPIO_PULL_UP) /* KSI2/GPIO27/TRACEDATA1, KSI3/GPIO26/TRACEDATA0, KSI4/GPIO25/TRACECLK/GP_SCLK, KSI5/GPIO24/GP_MISO, KSI6/GPIO23/S_SBUB, KSI7/GPIO22/S_SBUA */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KB, GPIO_ODR_HIGH)    /* KSO00/GPIO21/JTAG_TCK_SWCLK, KSO01/GPIO20/JTAG_TMS_SWIO */
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KB, GPIO_INPUT | GPIO_PULL_UP) /* KSI0/GPIO31/TRACEDATA3/GP_MOSI, KSI1/GPIO30/TRACEDATA2/GP_CS_L */
ALTERNATE(PIN_MASK(8, 0x04), 0, MODULE_KB, GPIO_ODR_HIGH)    /* KSO14/GPIO82 */

/* PMU alternate functions */
ALTERNATE(PIN_MASK(0, 0x01), 0, MODULE_PMU, GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH) /* PSL_IN2_L&GPI00/GPIO00 */
ALTERNATE(PIN_MASK(0, 0x02), 0, MODULE_PMU, GPIO_INT_BOTH | GPIO_HIB_WAKE_LOW) /* GPIO01/PSL_IN3_L&GPI01 */
ALTERNATE(PIN_MASK(D, 0x04), 0, MODULE_PMU, GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH) /* PSL_IN1_L&GPID2/GPIOD2 */

/* Unused Pins */
UNUSED(PIN(D, 6))       /* GPOD6/CR_SOUT3/SHDF_ESPI_L */
UNUSED(PIN(D, 0))
UNUSED(PIN(D, 1))
UNUSED(PIN(3, 2))       /* GPO32/TRIS_L */
UNUSED(PIN(3, 5))       /* GPO35/CR_SOUT4/TEST_L */
UNUSED(PIN(6, 6))       /* GPIO66 */
UNUSED(PIN(8, 1))       /* GPIO81 */
UNUSED(PIN(E, 1))       /* GPIOE1 */
UNUSED(PIN(E, 4))
UNUSED(PIN(0, 2))       /* USB_C1_TCPC_RST_ODL. Not connected because R564 is
                           DNS. */
