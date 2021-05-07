/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-P-RVP-MCHP1727 board-specific configuration */

#include "baseboard/intelrvp/adlrvp_ioex_gpio.inc"
/* include common gpio.inc under chip/mchp/lfw/... */
#include "chip/mchp/lfw/gpio.inc"

/* Power sequencing interrupts */
GPIO_INT(ALL_SYS_PWRGD,		PIN(057), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(RSMRST_PWRGD,		PIN(0221), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S0_N,		PIN(0243), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(VCCPDSW_3P3,		PIN(0201), GPIO_INT_BOTH, power_signal_interrupt)
/* TODO: GPIO_INT(VCCST_PWRGD_MECC,	PIN(0207), GPIO_INT_BOTH, power_signal_interrupt) */
GPIO_INT(PM_SLP_SUS_EC_N, 	PIN(0227), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
GPIO_INT(PM_SLP_S3_N,		PIN(0161), GPIO_INT_BOTH, power_signal_interrupt)
#else
GPIO(PM_SLP_S3_N,		PIN(0161), GPIO_INPUT)
#endif
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S4
GPIO_INT(PM_SLP_S4_N,		PIN(0162), GPIO_INT_BOTH, power_signal_interrupt)
#else
GPIO(PM_SLP_S4_N,		PIN(0162), GPIO_INPUT)
#endif

/* Button interrupts */
GPIO_INT(VOLUME_UP,		PIN(036), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(VOL_DN_EC,		PIN(0254), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(SMC_LID,		PIN(0226), GPIO_INT_BOTH | GPIO_PULL_UP, lid_interrupt)
GPIO_INT(MECH_PWR_BTN_ODL,	PIN(0115), GPIO_INT_BOTH, power_button_interrupt)

/* DC / AC interrupts, DC Jack presence coming from +VADP_OUT */
GPIO_INT(STD_ADP_PRSNT, 	PIN(043), GPIO_INT_BOTH, board_dc_jack_interrupt) /* DC_JACK_PRESENT */
GPIO_INT(BC_ACOK,		PIN(0156), GPIO_INT_BOTH, extpower_interrupt) /* AC Present */

/* Sensor interrupt */
GPIO_INT(SLATE_MODE_INDICATION,	PIN(0222), GPIO_INT_BOTH, gmr_tablet_switch_isr)

/* Flash WP interrupt */
GPIO_INT(EC_FLASH_WP_ODL,	PIN(014), GPIO_INT_BOTH, switch_interrupt)

/* Case Closed Debug Mode interrupt */
GPIO_INT(CCD_MODE_ODL,		PIN(0175), GPIO_INT_BOTH, board_connect_c0_sbu)

/* UART0 RX interrupt, RX input wake event */
GPIO_INT(UART0_RX, PIN(0105), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, uart_deepsleep_interrupt)

/* USB-C interrupts */
GPIO_INT(USBC_TCPC_ALRT_P0,	PIN(0143), GPIO_INT_BOTH, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P0, PIN(0240), GPIO_INT_BOTH, ppc_interrupt)

#if defined(HAS_TASK_PD_C1)
GPIO_INT(USBC_TCPC_ALRT_P1,	PIN(0241), GPIO_INT_BOTH, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P1, PIN(0101), GPIO_INT_BOTH, ppc_interrupt)
#else
GPIO(USBC_TCPC_ALRT_P1,	PIN(0241), GPIO_INPUT)
GPIO(USBC_TCPC_PPC_ALRT_P1,	PIN(0101), GPIO_INPUT)
#endif

#if defined(HAS_TASK_PD_C2)
/* w/o rework: USBC_TCPC_ALRT_P2 -> VCI_OVRD_IN */
GPIO_INT(USBC_TCPC_ALRT_P2,	PIN(0130), GPIO_INT_BOTH, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P2, PIN(0144), GPIO_INT_BOTH, ppc_interrupt)
#else
GPIO(USBC_TCPC_ALRT_P2,	PIN(0130), GPIO_INPUT)
GPIO(USBC_TCPC_PPC_ALRT_P2,	PIN(0144), GPIO_INPUT)
#endif

#if defined(HAS_TASK_PD_C3)
GPIO_INT(USBC_TCPC_ALRT_P3,	PIN(0242), GPIO_INT_BOTH, tcpc_alert_event)
GPIO_INT(USBC_TCPC_PPC_ALRT_P3, PIN(0142), GPIO_INT_BOTH, ppc_interrupt)
#else
GPIO(USBC_TCPC_ALRT_P3,	PIN(0242), GPIO_INPUT)
GPIO(USBC_TCPC_PPC_ALRT_P3,	PIN(0142), GPIO_INPUT)
#endif

/* Host communication GPIOs */
GPIO(SMC_WAKE_SCI_N_MECC,	PIN(051), GPIO_ODR_HIGH)
GPIO(EC_PCH_MKBP_INT_ODL,	PIN(0127), GPIO_ODR_HIGH)
#ifndef CONFIG_HOSTCMD_ESPI
GPIO(LPC_ESPI_RST_N,		PIN(061), GPIO_INPUT)
GPIO(PLT_RST_L, 		PIN(052), GPIO_INPUT) /* PCH_PLTRST_L */
#endif

/* Prochot GPIO */
GPIO(PROCHOT_EC_N,		PIN(002), GPIO_INPUT)

/* Power sequencing GPIOs */
GPIO(SYS_RST_ODL,		PIN(060), GPIO_ODR_HIGH)
GPIO(PM_RSMRST_N,		PIN(054), GPIO_OUT_LOW)
GPIO(PM_PWRBTN_N,		PIN(016), GPIO_ODR_HIGH)
/* w/o rework: EC_SPI_OE_MECC -> BGPO0 */
GPIO(EC_SPI_OE_MECC,		PIN(042), GPIO_OUT_LOW)
/* w/o rework: EC_DS3 -> VCI_OUT2 */
GPIO(EC_DS3,			PIN(025), GPIO_OUT_LOW)
UNIMPLEMENTED(EN_PP5000)

/* PCH_PWROK_EC is an input, as it's driven by the Silego chip on RVP */
GPIO(PCH_PWROK_EC,	PIN(0106), GPIO_INPUT)
GPIO(SYS_PWROK,		PIN(0202), GPIO_OUT_LOW)
GPIO(EC_DSW_PWROK,	PIN(034), GPIO_OUT_LOW)

/* H1 GPIOs */
GPIO(EC_H1_PACKET_MODE,		PIN(035), GPIO_OUT_LOW)
GPIO(EC_ENTERING_RW,		PIN(0102), GPIO_OUT_LOW)

/* Battery present */
GPIO(BAT_DET,		PIN(0206), GPIO_INPUT)

/* EDP */
GPIO(EDP_BKLT_EN_MECC,	PIN(022), GPIO_OUT_HIGH)

/* LED */
GPIO(LED_1_L,		PIN(0157), GPIO_OUT_HIGH)
GPIO(LED_2_L,		PIN(0153), GPIO_OUT_HIGH)

/* FAN control pins */
GPIO(THERM_SEN_MECC,	PIN(0141), GPIO_OUT_LOW)

/*
 * I2C pins should be configure as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SMB_BS_CLK,		PIN(004), GPIO_INPUT)	/* I2C00_SCL */
GPIO(SMB_BS_DATA,		PIN(003), GPIO_INPUT)	/* I2C00_SDA */
GPIO(USBC_TCPC_I2C_CLK_P0,	PIN(0140), GPIO_INPUT)	/* I2C06_SCL */
GPIO(USBC_TCPC_I2C_DATA_P0,	PIN(0132), GPIO_INPUT)	/* I2C06_SDA */
GPIO(USBC_TCPC_I2C_CLK_P1,	PIN(013), GPIO_INPUT)	/* I2C07_SCL */
GPIO(USBC_TCPC_I2C_DATA_P1,	PIN(012), GPIO_INPUT)	/* I2C07_SDA */
GPIO(USBC_TCPC_I2C_CLK_P2,	PIN(010), GPIO_INPUT)	/* I2C03_SCL */
GPIO(USBC_TCPC_I2C_DATA_P2,	PIN(007), GPIO_INPUT)	/* I2C03_SDA */
GPIO(USBC_TCPC_I2C_CLK_P3,	PIN(0155), GPIO_INPUT)	/* I2C02_SCL */
GPIO(USBC_TCPC_I2C_DATA_P3,	PIN(0154), GPIO_INPUT)	/* I2C02_SDA */

/* Unused pins */
GPIO(SML1_CLK_MECC,		PIN(0131), GPIO_INPUT)
GPIO(CPU_CAT_ERR_MECC,		PIN(000), GPIO_INPUT)
GPIO(ESPI_ALERT0_N,		PIN(063), GPIO_INPUT)
GPIO(BATT_DISABLE_EC,		PIN(067), GPIO_INPUT)
/* Unused: GPIO(SLP_S0_CS_N,		PIN(nVCI_IN0), GPIO_INPUT) */
GPIO(CPU_C10_GATE_MECC,		PIN(023), GPIO_INPUT)
GPIO(SMC_SDOWN_MECC,		PIN(0255), GPIO_INPUT)
GPIO(STD_ADPT_CNTRL_GPIO,	PIN(0244), GPIO_INPUT)
GPIO(SMC_ONOFF_N,		PIN(0114), GPIO_INPUT) /* Power button interrupt without H1 */
GPIO(SUSWARN,			PIN(024), GPIO_INPUT)
GPIO(ME_G3_TO_M3_EC,		PIN(033), GPIO_INPUT)

/* Alternate pins for I2C */
/* I2C pins alternate functions for ports 0, 2-3, 6-7
 * Configure I2C ports as I2C alternate function.
 * If board does not use external pull-ups then change GPIO flags
 * to enable internal pull-ups.
 * I2C00_SDA = GPIO_0003(Bank=0, bit=3) Func1
 * I2C00_SCL = GPIO_0004(Bank=0, bit=4) Func1
 * I2C02_SDA = GPIO_0154(Bank=3, bit=12) Func1
 * I2C02_SCL = GPIO_0155(Bank=3, bit=13) Func1
 * I2C03_SDA = GPIO_0007(Bank=0, bit=7) Func1
 * I2C03_SDL = GPIO_0010(Bank=0, bit=8) Func1
 * I2C06_SDA = GPIO_0132(Bank=2, bit=26) Func1
 * I2C06_SCL = GPIO_0140(Bank=3, bit=0) Func1
 * I2C07_SDA = GPIO_0012(Bank=0, bit=10) Func1
 * I2C07_SDL = GPIO_0013(Bank=0, bit=11) Func1
 */
ALTERNATE(PIN_MASK(0,  0x00000D98), 1, MODULE_I2C, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(2,  0x04000000), 1, MODULE_I2C, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(3,  0x00003001), 1, MODULE_I2C, GPIO_ODR_HIGH)

/* Alternate pins for ADC */
/* VR_TMP/V_1P05 - GPIO44, SKIN_THERM_IN_EC - GPIO43, AMBIENT_TEMP - GPIO42, DDR_TEMP/EC_5V - GPIO41 */
/* ADC pins
 * GPIO200/ADC00 = VR_TMP/V_1P05
 * GPIO203/ADC03 = AMBIENT_TEMP
 * GPIO204/ADC04 = SKIN_THERM_IN_EC
 * GPIO205/ADC05 = DDR_TEMP/EC_5V
 * ADC00 = GPIO_0200(Bank=4, bit=0) Func1
 * ADC03 = GPIO_0203(Bank=4, bit=3) Func1
 * ADC04 = GPIO_0204(Bank=4, bit=4) Func1
 * ADC05 = GPIO_0205(Bank=4, bit=5) Func1
 */
ALTERNATE(PIN_MASK(4,  0x0039), 1, MODULE_ADC, GPIO_ANALOG)

/* Alternate pins for FAN */
/* mchp_todo: GPIO053/GPWM0 for RPM-FAN */
/* mchp_todo: GPIO050/GTACH0 for RPM-FAN */

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
#define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_LOW)
/*
 * MEC172xH-SZ (144 pin package)
 * KSO00 = GPIO_0040 Func2 bank 1 bit 0
 * KSO01 = GPIO_0045 Func1 bank 1 bit 5
 * KSO02 = GPIO_0046 Func1 bank 1 bit 6
 * KSO03 = GPIO_0047 Func1 bank 1 bit 7
 * KSO04 = GPIO_0107 Func2 bank 2 bit 7
 * KSO05 = GPIO_0112 Func1 bank 2 bit 10
 * KSO06 = GPIO_0113 Func1 bank 2 bit 11
 * KSO07 = GPIO_0120 Func1 bank 2 bit 16
 * KSO08 = GPIO_0121 Func2 bank 2 bit 17
 * KSO09 = GPIO_0122 Func2 bank 2 bit 18
 * KSO10 = GPIO_0123 Func2 bank 2 bit 19
 * KSO11 = GPIO_0124 Func2 bank 2 bit 20
 * KSO12 = GPIO_0125 Func2 bank 2 bit 21
 * For 8x16 keyboard add KSO13 - KSO15
 * KSO13 = GPIO_0126 Func2 bank 2 bit 22
 * KSO14 = GPIO_0152 Func1 bank 3 bit 10
 * KSO15 = GPIO_0151 Func2 bank 3 bit 9
 *
 * KSI0 = GPIO_0017 Func1 bank 0 bit 15
 * KSI1 = GPIO_0020 Func1 bank 0 bit 16
 * KSI2 = GPIO_0021 Func1 bank 0 bit 17
 * KSI3 = GPIO_0026 Func1 bank 0 bit 22
 * KSI4 = GPIO_0027 Func1 bank 0 bit 23
 * KSI5 = GPIO_0030 Func1 bank 0 bit 24
 * KSI6 = GPIO_0031 Func1 bank 0 bit 25
 * KSI7 = GPIO_0032 Func1 bank 0 bit 26
 */
/* KSO 0 Bank 1, Func2, bit 0 */
ALTERNATE(PIN_MASK(1, 0x01), 2, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)

#ifdef CONFIG_KEYBOARD_COL2_INVERTED
/* KSO 1-3 Bank 1, Func1, bits 5-7 */
ALTERNATE(PIN_MASK(1, 0xA0), 1, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
GPIO(KBD_KSO2, PIN(046), GPIO_KB_OUTPUT_COL2)
#else
/* KSO 1-3 Bank 1, Func1, bits 5-7 */
ALTERNATE(PIN_MASK(1, 0xE0), 1, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
#endif

/* KSO 4, 8-12 Bank 2, Func2, bits 7, 17-21 */
ALTERNATE(PIN_MASK(2, 0x003E0080), 2, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
/* KSO 5-7, Bank 2, Func1, bits 10-11, 16 */
ALTERNATE(PIN_MASK(2, 0x00010C00), 1, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)

/* KSI 0-7, Bank 0, Func1, bit 15-17, 22-26 */
ALTERNATE(PIN_MASK(0, 0x07C38000), 1, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)

/*
 * ESPI_RESET# - GPIO_0061 Func 1, Bank 1 bit[17]
 * ESPI_ALERT# - GPIO_0063 Func 1, Bank 1 bit[19]
 * ESPI_CLK    - GPIO_0065 Func 1, Bank 1 bit[21]
 * ESPI_CS#    - GPIO_0066 Func 1, Bank 1 bit[22]
 * ESPI_IO0    - GPIO_0070 Func 1, Bank 1 bit[24]
 * ESPI_IO1    - GPIO_0071 Func 1, Bank 1 bit[25]
 * ESPI_IO2    - GPIO_0072 Func 1, Bank 1 bit[26]
 * ESPI_IO3    - GPIO_0073 Func 1, Bank 1 bit[27]
 */
ALTERNATE(PIN_MASK(1, 0x0F6A0000), 1, MODULE_LPC, 0)
