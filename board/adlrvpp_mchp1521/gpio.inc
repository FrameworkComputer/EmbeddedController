/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADLRVP-P-DDR4-MEC1521 board-specific configuration. */

/*
 * MEC152X data sheets GPIO numbers are OCTAL.
 */

/*
 * MEC1521H-SZ MECC board SPI flash is connected to MEC1521H-SZ shared SPI port.
 * GPIO055/SHD_CS0# pin is used to determine the boot source (eSPI Flash channel
 * or shared SPI). On RVP, SHD_CS0_N is connected to Flash0 and SHD_CS1_N is
 * connected to Flash1. Based on RVP design, Flash1 is meant for EC & Flash0
 * for Coreboot.But, for MEC, it is mandatory to route CS0 to external Flash
 * for EC to boot load the image. Hence, necessary rework must be taken care to
 * route CS0 to external Flash1.
 */
/* Include common gpios needed for LFW loader and main process FW */
#include "chip/mchp/lfw/gpio.inc"

#include "baseboard/intelrvp/adlrvp_ioex_gpio.inc"

/* As all signal names used here are supposed to match the schematic,
 * few pin names may not indicate the real purpose.Refer comments to clarify the pin usage.
 * Such pins are further renamed in board.h.
 */

/* Power sequencing interrupts */
GPIO_INT(ALL_SYS_PWRGD,		PIN(057),	GPIO_INT_BOTH,	power_signal_interrupt)
GPIO_INT(RSMRST_PWRGD_EC_N,	PIN(012),	GPIO_INT_BOTH,	power_signal_interrupt)
GPIO_INT(PM_SLP_S0_R_N,		PIN(0243),	GPIO_INT_BOTH,	power_signal_interrupt)
/* Below meaningless pin names are to match the schematic.*/
/* PG_EC_DSW_PWROK signal */
GPIO_INT(EC_TRACE_DATA_2,	PIN(0202),	GPIO_INT_BOTH,	power_signal_interrupt)
/* VCCST_PWRGD signal */
GPIO_INT(EC_TRACE_DATA_3,	PIN(0203),	GPIO_INT_BOTH,	power_signal_interrupt)
GPIO_INT(PM_SLP_SUS_N,		PIN(000),	GPIO_INT_BOTH,	power_signal_interrupt)

/* Button interrupts */
GPIO_INT(VOL_UP_EC,		PIN(0242),	GPIO_INT_BOTH | GPIO_PULL_UP,	button_interrupt)
GPIO_INT(VOL_DOWN_EC,		PIN(0246),	GPIO_INT_BOTH | GPIO_PULL_UP,	button_interrupt)
GPIO_INT(SMC_LID,		PIN(033),	GPIO_INT_BOTH | GPIO_PULL_UP,	lid_interrupt)
/* Buffered power button input from
 * PMIC-SLG7NT4192VTR / PWRBTN_EC_SILEGO_OUTPUT */
GPIO_INT(PWRBTN_EC_IN_N,	PIN(0163),	GPIO_INT_BOTH,	power_button_interrupt)

/* DC Jack presence coming from +VADP_OUT */
/* DC_JACK_PRESENT */
GPIO_INT(STD_ADP_PRSNT_EC,	PIN(052),	GPIO_INT_BOTH,	board_dc_jack_interrupt)
/* AC Present */
GPIO_INT(BC_ACOK_EC_IN,		PIN(0172),	GPIO_INT_BOTH,	extpower_interrupt)

/* H1 */
/* WP_L interrupt pin */
GPIO_INT(EC_WAKE_CLK_R,		PIN(0241),	GPIO_INT_BOTH,	switch_interrupt)

/* Case Closed Debug Mode */
/* CCD_MODE_ODL pin */
GPIO_INT(KBC_NUMLOCK,		PIN(0255),	GPIO_INT_BOTH,	board_connect_c0_sbu)

/* USB-C interrupts */
/* Below meaningless pin names are to match the schematic */
/* TCPC & PPC Alerts */
/* USBC_TCPC_ALRT_P0 */
GPIO_INT(TYPEC_EC_SMBUS_ALERT_0_R,	PIN(0132),	GPIO_INT_BOTH,	tcpc_alert_event)
/* USBC_TCPC_ALRT_P1 */
GPIO_INT(TYPEC_EC_SMBUS_ALERT_1_R,	PIN(0245),	GPIO_INT_BOTH,	tcpc_alert_event)
/* USBC_TCPC_PPC_ALRT_P0 */
GPIO_INT(KBC_SCANOUT_15,	PIN(0151),	GPIO_INT_BOTH,	ppc_interrupt)
/* USBC_TCPC_PPC_ALRT_P1 */
GPIO_INT(KBC_CAPSLOCK,		PIN(0127),	GPIO_INT_BOTH,	ppc_interrupt)

/* Sensor Interrupts */
/* GPIO_TABLET_MODE_L */
GPIO_INT(EC_SLATEMODE_HALLOUT_SNSR_R,	PIN(064),	GPIO_INT_BOTH |
					GPIO_SEL_1P8V,	gmr_tablet_switch_isr)

/* UART Interrupt */
GPIO_INT(EC_UART_RX,	PIN(0145),	GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,
						uart_deepsleep_interrupt)

/* Power sequencing GPIOs */
GPIO(PM_RSMRST_R,	PIN(054),	GPIO_OUT_LOW)
GPIO(PM_PWRBTN_N_R,	PIN(0101),	GPIO_ODR_HIGH)
GPIO(EC_PCH_SPI_OE_N,	PIN(024),	GPIO_OUT_LOW)
GPIO(SYS_PWROK_EC_R,	PIN(043),	GPIO_OUT_LOW)
/* Below names are meaningless to match schematic */
/* PCH_DSW_PWROK */
GPIO(EC_TRACE_DATA_1,	PIN(0201),	GPIO_OUT_LOW)
/* SYS_RESET_L */
GPIO(DG2_PRESENT,	PIN(0165),	GPIO_ODR_HIGH)
/* EN_PP3300_A */
GPIO(EC_DS3_R,		PIN(0226),	GPIO_OUT_LOW)
/* PCH_PWROK_EC is an input, as it is driven by the Silego chip on RVP */
GPIO(EC_PM_PCH_PWROK,	PIN(0106),	GPIO_INPUT)
UNIMPLEMENTED(EN_PP5000)

/* Host communication GPIOs */
/* PCH_WAKE_L pin*/
GPIO(SMC_WAKE_SCI_N,	PIN(0114),	GPIO_ODR_HIGH)
/* EC_INT_L pin */
GPIO(EC_TRACE_DATA_0,	PIN(0200),	GPIO_ODR_HIGH)
#ifndef CONFIG_HOST_INTERFACE_ESPI
GPIO(ESPI_RST_EC_R_N,	PIN(061),	GPIO_INPUT)
#endif

/* H1 Pins */
/* PACKET_MODE_EN */
GPIO(EC_TRACE_CLK,	PIN(0105),	GPIO_OUT_LOW)
/* ENTERING_RW */
GPIO(DNX_FORCE_RELOAD_EC_R,	PIN(0115),	GPIO_OUT_LOW)

/* FAN control pins */
GPIO(FAN_PWR_DISABLE,	PIN(060),	GPIO_OUT_LOW)

/* EDP */
GPIO(EC_EDP1_BKLT_EN,	PIN(0157),	GPIO_OUT_HIGH)

/* LEDs */
GPIO(PM_BAT_STATUS_LED2,	PIN(035),	GPIO_OUT_LOW)
GPIO(PM_PWRBTN_LED,		PIN(0254),	GPIO_OUT_LOW)

/* Battery present */
GPIO(BATT_ID_R,		PIN(0162),	GPIO_INPUT)

/* VTR2 STRAP PIN - GPIO0104_UART0_TX
 * Voltage level strap used to determine,
 * if shared flash interface must be configured
 * for 3.3V or 1.8V.
 * 1 = 3.3V operation
 * 0 = 1.8V operation
 */
GPIO(EC_VTR2_STRAP,	PIN(0104),	GPIO_INPUT)

/* Tap Controller select strap - GPIO0170_UART1_TX
 * If any of the JTAG TAP controllers are used, GPIO170 must only
 * be configured as an output to a VTRx powered external function.
 * GPIO170 may only be configured as an input when
 * the JTAG TAP controllers are not needed or when an external driver
 * does not violate the Slave Select Timing.
 */
GPIO(EC_JTAG_STRAP,	PIN(0170),	GPIO_ODR_HIGH)

/* I2C pins */
/* I2C pins should be configure as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SMB_BS_CLK,		PIN(004),	GPIO_INPUT)
GPIO(SMB_BS_DATA,		PIN(003),	GPIO_INPUT)
GPIO(TYPEC_EC_SMBUS1_CLK_EC,	PIN(0131),	GPIO_INPUT)
GPIO(TYPEC_EC_SMBUS1_DATA_EC,	PIN(0130),	GPIO_INPUT)
GPIO(TYPEC_EC_SMBUS3_DATA,	PIN(0142),	GPIO_INPUT)
GPIO(TYPEC_EC_SMBUS3_CLK,	PIN(0141),	GPIO_INPUT)

/* Unused Pins */
/* These have different meaning on RVP for alternate OS
 * support.Hence, keep them as input.
 */
GPIO(PROCHOT_EC_R,		PIN(0253),	GPIO_INPUT)
GPIO(BC_PROCHOT_EC_N,		PIN(034),	GPIO_INPUT)
GPIO(EC_PEG_PLI_N_DG2,		PIN(036),	GPIO_INPUT)
GPIO(PEG_PIM_DG2,		PIN(0240),	GPIO_INPUT)
GPIO(KBD_BKLT_CTRL,		PIN(014),	GPIO_INPUT)
GPIO(STD_ADPT_CNTRL_GPIO_R,	PIN(0171),	GPIO_INPUT)
GPIO(SAF_G3_DETECT,		PIN(013),	GPIO_INPUT)
GPIO(EC_GPIO015_PWM7,		PIN(015),	GPIO_INPUT)
GPIO(EC_GPPC_B14,		PIN(0244),	GPIO_INPUT)
GPIO(SX_EXIT_HOLDOFF_N,		PIN(0175),	GPIO_INPUT)
GPIO(MIC_PRIVACY_EC,		PIN(0100),	GPIO_INPUT)
GPIO(MIC_PRIVACY_SWITCH,	PIN(011),	GPIO_INPUT)
GPIO(PECI_MUX_CTRL_ER_INT,	PIN(025),	GPIO_INPUT)
GPIO(PEG_RTD3_COLD_MOD_SW_R,	PIN(0156),	GPIO_INPUT)
GPIO(H_CATERR_EC_N,		PIN(0153),	GPIO_INPUT)
GPIO(PM_BATLOW_N,		PIN(0140),	GPIO_INPUT)
GPIO(HOME_BTN_EC_RVP_AEP_ID,	PIN(023),	GPIO_INPUT)
GPIO(CPU_C10_GATE_N_R,		PIN(022),	GPIO_INPUT)
GPIO(VREF_VTT,			PIN(044),	GPIO_INPUT)
GPIO(HB_NVDC_SEL,		PIN(0161), 	GPIO_INPUT)
GPIO(EC_VCI_OUT_WAKE_R,		PIN(0250),	GPIO_INPUT)
GPIO(PM_SLP_S0_CS_N,		PIN(0221),	GPIO_INPUT)
GPIO(GPPC_E7_EC_SMI_N_R,	PIN(0102),	GPIO_INPUT)
GPIO(TC_RETIMER_FORCE_PWR_BTP_EC_R,	PIN(0222),	GPIO_INPUT)

/* Alternate functions */

/* Alternate functions GPIO definitions */

/*
 * MCHP has 6 banks/ports each containing 32 GPIO's.
 * Each bank/port is connected to a GIRQ.
 * Port numbering Example:
 * GPIO_015 = 13 decimal. Port/bank = 13/32 = 0, bit = 13 % 32 = 13
 * GPIO_0123 = 83 decimal. Port/bank = 83/32 = 2, bit = 83 % 32 = 19
 * OR port = 0123 >> 5, bit = 0123 & 037(0x1F) = 023 = 19 decimal.
 */

/*
 * Configure I2C as alternate function
 * I2C00_SDA = GPIO_0003(Bank=0, bit=3) Func1
 * I2C00_SCL = GPIO_0004(Bank=0, bit=4) Func1
 * I2C01_SDA = GPIO_0130(Bank=2, bit=24) Func1
 * I2C01_SCL = GPIO_0131(Bank=2, bit=25) Func1
 * I2C04_SDA = GPIO_0143(Bank=3, bit=3) Func1
 * I2C04_SCL = GPIO_0144(Bank=3, bit=4) Func1
 * I2C05_SDA = GPIO_0141(Bank=3, bit=1) Func1
 * I2C05_SDL = GPIO_0142(Bank=3, bit=2) Func1
 */
ALTERNATE(PIN_MASK(0, 0x00000018),	GPIO_ALT_FUNC_1,	MODULE_I2C,
								GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(2, 0x03000000),	GPIO_ALT_FUNC_1,	MODULE_I2C,
								GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(3, 0x0000001E),	GPIO_ALT_FUNC_1,	MODULE_I2C,
								GPIO_ODR_HIGH)

/* Alternate pins for ADC */
/*
 * ADC04 - ADC07
 * GPIO_0204 Func 1 = ADC04 - AMBIET_THERM_IN0
 * GPIO_0205 Func 1 = ADC05 - VR_THERM_IN
 * GPIO_0206 Func 1 = ADC06 - DDR_THERM_IN
 * GPIO_0207 Func 1 = ADC07 - SKIN_THERM_IN1
 * Bank 4 bits[4:5:6:7]
 */
ALTERNATE(PIN_MASK(4, 0x00000F0),	GPIO_ALT_FUNC_1,	MODULE_ADC,
								GPIO_ANALOG)
/*
 * VREF2_ADC_R
 * GPIO_067 Func 1 = VREF2_ADC
 * Bank 1 bit [23]
 */
ALTERNATE(PIN_MASK(1, 0x0800000),	GPIO_ALT_FUNC_1,	MODULE_ADC,
								GPIO_ANALOG)

/* Alternate pins for FAN */
/*
 * GPIO_050 Func 1 = TACH0 - CPU_TACHO_FAN
 * GPIO_053 Func 1 = PWM0 - CPU_PWM_FAN
 * Bank 1 bits[8:11]
 */
ALTERNATE(PIN_MASK(1, 0x0000900),	GPIO_ALT_FUNC_1,	MODULE_PWM,
								GPIO_FLAG_NONE)

/* KB pins */
/*
 * MEC1521H-SZ (144 pin package)
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
 * For 8x16 test keyboard add KSO13 - KSO15
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
ALTERNATE(PIN_MASK(1, 0x01),		2,	MODULE_KEYBOARD_SCAN,
							GPIO_ODR_HIGH)

#ifdef CONFIG_KEYBOARD_COL2_INVERTED
/* KSO 1-3 Bank 1, Func1, bits 5-7 */
ALTERNATE(PIN_MASK(1, 0xA0),		1,	MODULE_KEYBOARD_SCAN,
							GPIO_ODR_HIGH)
GPIO(KBD_KSO2,		PIN(046),	GPIO_OUT_LOW)
#else
/* KSO 1-3 Bank 1, Func1, bits 5-7 */
ALTERNATE(PIN_MASK(1, 0xE0),		1,	MODULE_KEYBOARD_SCAN,
							GPIO_ODR_HIGH)
#endif

/* KSO 4, 8-12 Bank 2, Func2, bits 7, 17-21 */
ALTERNATE(PIN_MASK(2, 0x003E0080),	2,	MODULE_KEYBOARD_SCAN,
							GPIO_ODR_HIGH)
/* KSO 5-7, Bank 2, Func1, bits 10-11, 16 */
ALTERNATE(PIN_MASK(2, 0x00010C00),	1,	MODULE_KEYBOARD_SCAN,
							GPIO_ODR_HIGH)

/* KSI 0-7, Bank 0, Func1, bit 15-17, 22-26 */
ALTERNATE(PIN_MASK(0, 0x07C38000),	1,	MODULE_KEYBOARD_SCAN,
						(GPIO_INPUT | GPIO_PULL_UP))

/*
 * ESPI_RST_EC_R_N# - GPIO_0061 Func 1, Bank 1 bit[17]
 * ESPI_ALERT0_EC_R_N# - GPIO_0063 Func 1, Bank 1 bit[19]
 * ESPI_CLK_EC_R    - GPIO_0065 Func 1, Bank 1 bit[21]
 * ESPI_CS0_EC_R_N# - GPIO_0066 Func 1, Bank 1 bit[22]
 * ESPI_IO0_EC_R    - GPIO_0070 Func 1, Bank 1 bit[24]
 * ESPI_IO1_EC_R    - GPIO_0071 Func 1, Bank 1 bit[25]
 * ESPI_IO2_EC_R    - GPIO_0072 Func 1, Bank 1 bit[26]
 * ESPI_IO3_EC_R    - GPIO_0073 Func 1, Bank 1 bit[27]
 */
ALTERNATE(PIN_MASK(1, 0x0F6A0000),	1,	MODULE_LPC,	GPIO_FLAG_NONE)
