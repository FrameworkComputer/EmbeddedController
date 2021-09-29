/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-P-RVP-ITE board-specific configuration */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */

#include "baseboard/intelrvp/adlrvp_ioex_gpio.inc"

/* Power sequencing interrupts */
GPIO_INT(ALL_SYS_PWRGD_EC,	PIN(F, 0), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(RSMRST_PWRGD_EC,	PIN(F, 1), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S0_N,		PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(VCCPDSW_3P3_EC,	PIN(I, 3), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(VCCST_PWRGD,		PIN(I, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PM_SLP_SUS_EC, 	PIN(K, 2), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
GPIO_INT(SLP_S3_R_L,		PIN(F, 2), GPIO_INT_BOTH, power_signal_interrupt)
#endif
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S4
GPIO_INT(SLP_S4_R_L,		PIN(F, 3), GPIO_INT_BOTH, power_signal_interrupt)
#endif

/* Button interrupts */
GPIO_INT(VOLUME_UP,		PIN(D, 5), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(VOL_DN_EC_R,		PIN(D, 6), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(SMC_LID,		PIN(E, 2), GPIO_INT_BOTH | GPIO_PULL_UP, lid_interrupt)
GPIO_INT(MECH_PWR_BTN_ODL,	PIN(E, 4), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(EC_WP_ODL,		PIN(I, 4), GPIO_INT_BOTH, switch_interrupt)

/* DC Jack presence coming from +VADP_OUT */
GPIO_INT(STD_ADP_PRSNT, 	PIN(J, 2), GPIO_INT_BOTH, board_dc_jack_interrupt) /* DC_JACK_PRESENT */

GPIO_INT(BC_ACOK_EC,		PIN(K, 3), GPIO_INT_BOTH, extpower_interrupt) /* AC Present */

GPIO_INT(UART_SERVO_TX_EC_RX,	PIN(B, 0), GPIO_INT_FALLING, uart_deepsleep_interrupt)

/* USB-C interrupts */
/* Using embedded TCPC for Port-0 */
UNIMPLEMENTED(USBC_TCPC_ALRT_P0)
GPIO(NC_USBC_TCPC_ALRT_P0,	PIN(I, 7), GPIO_INPUT)
GPIO_INT(USBC_TCPC_PPC_ALRT_P0, PIN(J, 5), GPIO_INT_FALLING, ppc_interrupt)

/*
 * b:200265672 External pull-up resistor of port 1 is removed to fix port 3
 * TCPC interrupt issue. So add internal pull-up for port 1 instead.
 * This change is only applied to ITE chip.
 */
#if defined(HAS_TASK_PD_C1)
GPIO_INT(USBC_TCPC_ALRT_P1,	PIN(G, 0), GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P1, PIN(C, 4), GPIO_INT_FALLING, ppc_interrupt)
#else
GPIO(USBC_TCPC_ALRT_P1,		PIN(G, 0), GPIO_INPUT)
GPIO(USBC_TCPC_PPC_ALRT_P1,	PIN(C, 4), GPIO_INPUT)
#endif

#if defined(HAS_TASK_PD_C2)
GPIO_INT(USBC_TCPC_ALRT_P2,	PIN(J, 1), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P2, PIN(E, 5), GPIO_INT_FALLING, ppc_interrupt)
#else
GPIO(USBC_TCPC_ALRT_P2,		PIN(J, 1), GPIO_INPUT)
GPIO(USBC_TCPC_PPC_ALRT_P2,	PIN(E, 5), GPIO_INPUT)
#endif

#if defined(HAS_TASK_PD_C3)
GPIO_INT(USBC_TCPC_ALRT_P3,	PIN(J, 3), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P3, PIN(E, 6), GPIO_INT_FALLING, ppc_interrupt)
#else
GPIO(USBC_TCPC_ALRT_P3,		PIN(J, 3), GPIO_INPUT)
GPIO(USBC_TCPC_PPC_ALRT_P3,	PIN(E, 6), GPIO_INPUT)
#endif

#ifndef CONFIG_HOSTCMD_ESPI
GPIO_INT(ESPI_RST_R,		PIN(D, 2), GPIO_INPUT)
#endif

#ifdef CONFIG_HOSTCMD_ESPI
/* enable 1.8v input of EC's espi_reset pin, and then this pin takes effect. */
GPIO_INT(ESPI_RST_R,		PIN(D, 2), GPIO_INT_FALLING | GPIO_SEL_1P8V, espi_reset_pin_asserted_interrupt) /* eSPI_reset# */
#endif

/* Sensor Interrupts */
GPIO_INT(SLATE_MODE_INDICATION,	PIN(K, 1), GPIO_INT_BOTH | GPIO_SEL_1P8V, gmr_tablet_switch_isr)

/* Power sequencing GPIOs */
GPIO(SYS_RST_ODL_EC,		PIN(B, 6), GPIO_ODR_HIGH)
GPIO(PROCHOT_EC,		PIN(C, 0), GPIO_INPUT)
GPIO(PM_RSMRST_EC,		PIN(C, 6), GPIO_OUT_LOW)
GPIO(PM_PWRBTN_N_EC,		PIN(D, 0), GPIO_ODR_HIGH)
GPIO(EC_SPI_OE_N,		PIN(I, 2), GPIO_OUT_LOW)

GPIO(EC_PCH_MKBP_INT_ODL_EC,	PIN(B, 7), GPIO_ODR_HIGH)
GPIO(EDP_BKLT_EN,		PIN(J, 6), GPIO_OUT_HIGH)
GPIO(EC_DS3,			PIN(L, 4), GPIO_OUT_LOW)
UNIMPLEMENTED(EN_PP5000)

/*
 * PCH_SYS_PWROK is an input, driven by the Silego chip. The common x86
 * power sequencing expects that PCH_SYS_PWROK is an output and will drive
 * this signal if GPIO_PCH_SYS_PWROK is configured. Map this pin as no-connect
 * so that state can be monitored using the console.
 */
GPIO(PCH_PWROK_EC_R,	PIN(K, 4), GPIO_INPUT)
GPIO(SYS_PWROK_EC,	PIN(D, 1), GPIO_OUT_LOW)
GPIO(DSW_PWROK_EC,	PIN(L, 6), GPIO_OUT_LOW)

/* Host communication GPIOs */
#ifndef CONFIG_HOSTCMD_ESPI
GPIO(PLT_RST_L, 	PIN(H, 6), GPIO_INPUT | GPIO_PULL_UP) /* PCH_PLTRST_L */
#endif
GPIO(PCH_WAKE_N,	PIN(J, 0), GPIO_ODR_HIGH)

/* Battery present */
GPIO(BAT_DET_EC,	PIN(K, 0), GPIO_INPUT)

/* LED */
GPIO(LED_1_L_EC,	PIN(A, 6), GPIO_OUT_HIGH)	/* BAT_LED_GREEN_L LED_2_L */
GPIO(LED_2_L_EC,	PIN(A, 7), GPIO_OUT_HIGH)	/* AC_LED_GREEN_L LED_1_L */

/* H1 pins */
GPIO(EC_H1_PACKET_MODE_EC,	PIN(J, 4), GPIO_OUT_LOW)
GPIO(EC_ENTERING_RW_EC,		PIN(K, 7), GPIO_OUT_LOW)

/* Case Closed Debug Mode */
GPIO_INT(CCD_MODE_ODL,	PIN(B, 5), GPIO_INT_BOTH, board_connect_c0_sbu)

/* FAN control pins */
GPIO(EC_THRM_SEN_PWRGATE_N,	PIN(K, 6), GPIO_OUT_LOW)

/*
 * I2C pins should be configure as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(EC_I2C_PROG_SCL,		PIN(B, 3), GPIO_INPUT)	/* I2C_A_SCL */
GPIO(EC_I2C_PROG_SDA,		PIN(B, 4), GPIO_INPUT)	/* I2C_A_SDA */
GPIO(SMB_BS_CLK,		PIN(C, 1), GPIO_INPUT)	/* I2C_B_SCL */
GPIO(SMB_BS_DATA,		PIN(C, 2), GPIO_INPUT)	/* I2C_B_SDA */

/* P0 IT83XX_I2C_CH_C */
GPIO(USBC_TCPC_I2C_CLK_P0,	PIN(C, 7), GPIO_INPUT)	/* I2C_C_SCL */
GPIO(USBC_TCPC_I2C_DATA_P0,	PIN(F, 7), GPIO_INPUT)	/* I2C_C_SDA */

/* P1 IT83XX_I2C_CH_F */
GPIO(USBC_TCPC_I2C_CLK_P2,	PIN(A, 4), GPIO_INPUT)	/* I2C_F_SCL */
GPIO(USBC_TCPC_I2C_DATA_P2,	PIN(A, 5), GPIO_INPUT)	/* I2C_F_SDA */

/* P2 IT83XX_I2C_CH_E */
GPIO(USBC_TCPC_I2C_CLK_P1,	PIN(E, 0), GPIO_INPUT)	/* I2C_E_SCL */
GPIO(USBC_TCPC_I2C_DATA_P1,	PIN(E, 7), GPIO_INPUT)	/* I2C_E_SDA */

/* P3 IT83XX_I2C_CH_D */
GPIO(USBC_TCPC_I2C_CLK_P3,	PIN(H, 1), GPIO_INPUT)	/* I2C_D_SCL */
GPIO(USBC_TCPC_I2C_DATA_P3,	PIN(H, 2), GPIO_INPUT)	/* I2C_D_SDA */

/* Unused 1.8V or 3.3V compatiable pins */
GPIO(TP_DEVELOPER_MODE_EC,	PIN(B, 2), GPIO_INPUT)
GPIO(ESPI_CS1_N_R,		PIN(D, 3), GPIO_INPUT)
GPIO(SUSWARN,			PIN(E, 1), GPIO_INPUT)
GPIO(CPU_C10_GATE,		PIN(G, 1), GPIO_INPUT)
GPIO(TP_GPIO_G2,		PIN(G, 2), GPIO_INPUT)
GPIO(TP_GPIO_G6,		PIN(G, 6), GPIO_INPUT)
GPIO(BATT_DISABLE_EC,		PIN(H, 0), GPIO_INPUT)
GPIO(ME_G3_TO_M3_EC,		PIN(H, 5), GPIO_INPUT)
GPIO(SLP_S0_CS_EC,		PIN(I, 0), GPIO_INPUT)
GPIO(SMC_SHUTDOWN,		PIN(K, 5), GPIO_INPUT)
GPIO(CPU_CAT_ERR_MECC,		PIN(L, 1), GPIO_INPUT)
GPIO(SMC_ONOFF_N,		PIN(L, 3), GPIO_INPUT) /* Power button interrupt without H1 */
GPIO(STD_ADPT_CNTRL_EC,		PIN(L, 5), GPIO_INPUT)
GPIO(SMB_PCH_ALRT,		PIN(L, 7), GPIO_INPUT)
GPIO(ESPI_ALERT0_R,		PIN(M, 6), GPIO_INPUT | GPIO_SEL_1P8V)

/* Unused 3.3V compatiable pins */
GPIO(TP_GPIO_A0,		PIN(A, 0), GPIO_INPUT)
GPIO(TP_GPIO_A1,		PIN(A, 1), GPIO_INPUT)
GPIO(TP_GPIO_A3,		PIN(A, 3), GPIO_INPUT)
GPIO(TP_GPC3,			PIN(C, 3), GPIO_INPUT)
GPIO(TP_GPC5,			PIN(C, 5), GPIO_INPUT)
GPIO(TP_GPIO_E3,		PIN(E, 3), GPIO_INPUT)
GPIO(TP_GPIO_H3,		PIN(H, 3), GPIO_INPUT)
GPIO(TP_GPIO_H4,		PIN(H, 4), GPIO_INPUT)

/* Alternate pins for I2C */
ALTERNATE(PIN_MASK(A, BIT(4) | BIT(5)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)	/* I2C F SCL/SDA A4/A5 */
ALTERNATE(PIN_MASK(B, BIT(3) | BIT(4)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)	/* I2C A SCL/SDA B3/B4 */
ALTERNATE(PIN_MASK(C, BIT(1) | BIT(2)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)	/* I2C B SCL/SDA C1/C2 */
ALTERNATE(PIN_MASK(E, BIT(0) | BIT(7)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)	/* I2C E SCL/SDA E0/E7 */
ALTERNATE(PIN_MASK(C, BIT(7)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)		/* I2C C SCL C7 */
ALTERNATE(PIN_MASK(F, BIT(7)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)		/* I2C C SDA F7 */
ALTERNATE(PIN_MASK(H, BIT(1) | BIT(2)), GPIO_ALT_FUNC_DEFAULT, MODULE_I2C, GPIO_FLAG_NONE)	/* I2C D SCL/SDA H1/H2 */

/* Alternate pins for UART */
ALTERNATE(PIN_MASK(B, BIT(0) | BIT(1)), GPIO_ALT_FUNC_DEFAULT, MODULE_UART, GPIO_PULL_UP) 	/* UART1 B0/B1 */

/* Alternate pins for ADC */
ALTERNATE(PIN_MASK(I, BIT(1) | BIT(6)), GPIO_ALT_FUNC_DEFAULT, MODULE_ADC, GPIO_FLAG_NONE)	/* ADC 1,6 -> I1,I6 */
ALTERNATE(PIN_MASK(L, BIT(0) | BIT(2)), GPIO_ALT_FUNC_DEFAULT, MODULE_ADC, GPIO_FLAG_NONE)	/* ADC 13,15 -> L0,L2 */

/* Alternate pins for FAN */
ALTERNATE(PIN_MASK(A, BIT(2)), GPIO_ALT_FUNC_DEFAULT, MODULE_PWM, GPIO_FLAG_NONE)		/* PWM2 A2 */
ALTERNATE(PIN_MASK(D, BIT(7)), GPIO_ALT_FUNC_DEFAULT, MODULE_PWM, GPIO_FLAG_NONE)		/* TACH1A D7 */
