/* -*- mode:c -*-
 *
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * mec1701h_evb board GPIO pins
 * !!!!!!!!!!!!!!!!!!!!!!! IMPORTANT !!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * MEC1701 and MEC1322 data sheets GPIO numbers are OCTAL.
 * Original glados MEC1322 used these octal numbers as base 10.
 * mec1701 and its boards will use OCTAL therefore make sure all
 * numbers used below are written as C OCTAL with a leading 0.
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 */
/* NOTE: We use JTAG on MEC1701H on EVB.
 * GPIO_0145 is JTAG_TDI
 * GPIO_0146 is JTAG_TDO
 * GPIO_0147 is JTAG_CLK
 * GPIO_0150 is JTAG_TMS
 */

UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(RSMRST_L_PGOOD)
UNIMPLEMENTED(PMIC_DPWROK)
UNIMPLEMENTED(PCH_RTCRST)
UNIMPLEMENTED(SYS_RESET_L)
UNIMPLEMENTED(PMIC_SLP_SUS_L)
UNIMPLEMENTED(BAT_PRESENT_L)
UNIMPLEMENTED(PM_SLP_S3_L)
UNIMPLEMENTED(PM_SLP_S4_L)

/* Interrupt Pin Defination */

/* Power Sequence */
GPIO_INT(VCIN1_AC_IN,     PIN(0241), GPIO_INT_BOTH, extpower_interrupt)
GPIO_INT(LID_SW_L,        PIN(0215), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(ON_OFF,          PIN(0163), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(PWR_3V5V_PG,     PIN(0214), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_SUS_L,       PIN(0165), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(VCCIN_AUX_VR_PG, PIN(012),  GPIO_INT_BOTH, power_signal_interrupt)
/*GPIO_INT(PM_SLP_S3_L,     PIN(0225), GPIO_INT_BOTH, power_signal_interrupt)*/ /* eSPI signal */
/*GPIO_INT(PM_SLP_S4_L,     PIN(0166), GPIO_INT_BOTH, power_signal_interrupt)*/ /* eSPI signal */
GPIO_INT(VR_PWRGD,        PIN(0106), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SOC_ENBKL,       PIN(0211), GPIO_INT_BOTH, soc_signal_interrupt)

/* UART input */
GPIO_INT(UART0_RX,        PIN(0105), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, \
	 uart_deepsleep_interrupt)

/* Sensor */
/*GPIO_INT(EC_GPU_OVERT_L,  PIN(051),  GPIO_INT_FALLING, gpu_over_temperature_interrupt) */
GPIO_INT(EC_ALS_INT_L,    PIN(024),  GPIO_INT_FALLING, psensor_interrupt)
GPIO_INT(SOC_EC_INT_L,    PIN(0221), GPIO_INT_FALLING, soc_hid_interrupt)
GPIO_INT(EC_PD_INTA_L,    PIN(0152), GPIO_INT_FALLING, pd_chip_interrupt)
GPIO_INT(EC_PD_INTB_L,    PIN(0175), GPIO_INT_FALLING, pd_chip_interrupt)
GPIO_INT(EC_THM_ALERT_L,  PIN(0242), GPIO_INT_FALLING, thermal_sensor_interrupt)

/* Reserve Pin Defination */
GPIO(GPIO041,             PIN(041),  GPIO_OUT_LOW)
GPIO(GPIO101,             PIN(0101), GPIO_OUT_LOW)
GPIO(GPIO102,             PIN(0102), GPIO_OUT_LOW)
GPIO(GPIO172,             PIN(0172), GPIO_OUT_LOW)
GPIO(GPIO173,             PIN(0173), GPIO_OUT_LOW)
GPIO(GPIO231,             PIN(0231), GPIO_OUT_LOW)
GPIO(GPIO245,             PIN(0245), GPIO_OUT_LOW)
GPIO(GPIO254,             PIN(0254), GPIO_OUT_LOW)

GPIO(PLT_RST_L,           PIN(0140), GPIO_INPUT)    /* Not implement on reference board */
GPIO(EC_GPU_PROHOT_L,     PIN(0210), GPIO_INPUT)    /* Reserve for GPU GPIO9_Alert# first */
GPIO(EC_SLP_SOIX_L,       PIN(0230), GPIO_INPUT)    /* Use for modern standby status */
GPIO(GPIO233_STRAP,       PIN(0233), GPIO_INPUT)
GPIO(GPIO243_SW1,         PIN(0243), GPIO_INPUT)
GPIO(GPIO244_SW2,         PIN(0244), GPIO_INPUT)
GPIO(GPIO246_UPD_EN,      PIN(0246), GPIO_INPUT)

/* power sequence output pins */
/*
 * This pulldown should be removed and SLP_S0 should be enabled as a power
 * signal interrupt in future hardware followers. The signal is pulled up
 * in the SoC when the primary rails are on and/or ramping.
 * In order to not get interrupt storms there should be external logic
 * which makes this a true binary signal into the EC.
 */
GPIO(PBTN_OUT_L,            PIN(0213), GPIO_OUT_HIGH)
GPIO(AC_PRESENT_OUT,        PIN(0170), GPIO_OUT_LOW)
GPIO(EC_RSMRST_L,           PIN(0222), GPIO_OUT_LOW)
GPIO(EC_ON,                 PIN(023),  GPIO_OUT_LOW)
GPIO(PCH_DPWROK,            PIN(052),  GPIO_OUT_LOW)
GPIO(PCH_PWR_EN,            PIN(043),  GPIO_OUT_LOW)
GPIO(SYSON,                 PIN(0135), GPIO_OUT_LOW)
GPIO(SUSP_L,                PIN(0127), GPIO_OUT_LOW)
GPIO(EC_VCCST_PG,           PIN(0130), GPIO_OUT_LOW)
GPIO(VR_ON,                 PIN(013),  GPIO_OUT_LOW)
GPIO(PCH_PWROK,             PIN(0131), GPIO_OUT_LOW)
GPIO(SYS_PWROK,             PIN(057),  GPIO_OUT_LOW)


/* I2C pins - these will be reconfigured for alternate function below */
GPIO(SMB01_SCL, PIN(006),  GPIO_INPUT)
GPIO(SMB01_SDA, PIN(005),  GPIO_INPUT)
GPIO(SMB02_SCL, PIN(0155), GPIO_INPUT)
GPIO(SMB02_SDA, PIN(0154), GPIO_INPUT)
GPIO(SMB03_SCL, PIN(010),  GPIO_INPUT)
GPIO(SMB03_SDA, PIN(007),  GPIO_INPUT)
GPIO(SMB04_SCL, PIN(0144), GPIO_INPUT)
GPIO(SMB04_SDA, PIN(0143), GPIO_INPUT)
GPIO(SMB05_SCL, PIN(0142), GPIO_INPUT)
GPIO(SMB05_SDA, PIN(0141), GPIO_INPUT)

/* Fan PWM output and TACH input. PROCHOT input */
/* MEC1701H GPIO_0050/FAN_TACH0 OK */
GPIO(EC_FAN1_TTACH,         PIN(050), GPIO_INPUT | GPIO_PULL_UP)

/* Miscellaneous */
GPIO(EC_TP_DISABLE_L,       PIN(0110), GPIO_OUT_HIGH)   /* touchpad enable/disable */
GPIO(EC_KBL_PWR_EN,         PIN(0111), GPIO_OUT_HIGH)   /* keyboard backlight control */
GPIO(EC_WL_OFF_L,           PIN(0114), GPIO_OUT_LOW)
GPIO(EC_WLAN_EN,            PIN(0115), GPIO_OUT_HIGH)
GPIO(EC_IPCM_MUX_SEL,       PIN(002),  GPIO_OUT_LOW)    /* HW IPCM debug function */
GPIO(EC_IPCM_PWR_EN,        PIN(014),  GPIO_OUT_LOW)    /* HW IPCM debug function */
GPIO(ME_EN,                 PIN(060),  GPIO_ODR_HIGH)
GPIO(EC_SW_DEBUG_SEL,       PIN(025),  GPIO_OUT_LOW)    /* SW debug function */
GPIO(EC_SW_DEBUG_EN_L,      PIN(0240), GPIO_OUT_LOW)    /* SW debug function */
GPIO(EC_BKOFF_L,            PIN(0212), GPIO_OUT_HIGH)   /* Panel backlight control */
GPIO(EC_CODEC_MUTE_L,       PIN(0216), GPIO_ODR_HIGH)   /* Mute/Unmute audio amplifier */

/* Voltage compare function pin */
GPIO(VCIN1_ADP_PROCHOT,     PIN(0203), GPIO_INPUT)
GPIO(VCIN0_PD,              PIN(0205), GPIO_INPUT)
GPIO(VCOUT0_MAIN_PWR_ON,    PIN(0204), GPIO_OUT_HIGH)
GPIO(VCOUT1_PROCHOT_L,      PIN(0206), GPIO_OUT_HIGH)


/* SPI
 * Chip select must be open drain and driven high before SPI controller
 * configuration.
 */
/*
 * MEC1701H
 * GPIO_0055/PWM2/SHD_CS0#/RSMRST#
 * GPIO_0124/GPTP-OUT6/PVT_CS#/KSO11
 * QMSPI controller drives chip select, must be
 * configured to alternative function. See below.
 * Always use the name QMSPI_CS0 for chip select.
 * Actual GPIO could be GPIO_0055 QMSPI shared chip select or
 * GPIO_0124 private chip select.
 */
GPIO(QMSPI_CS0,		PIN(055),  GPIO_ODR_HIGH)

/*
 * MEC1701H GP-SPI0 chip select is GPIO_0003
 * It is used as GPIO output. GPSPI chip level
 * code drives chip select.
 */
GPIO(SPI0_CS0,		PIN(03),  GPIO_ODR_HIGH)


/*
 * TODO(crosbug.com/p/40848): These LEDs should be under control of the
 * mec1701 LED control unit. Remove these GPIO definitions once the LED
 * control unit is functional.
 */
GPIO(LED0_PWM,	PIN(0156), GPIO_OUT_LOW) /* Reserve */
GPIO(LED1_PWM,	PIN(0157), GPIO_OUT_LOW) /* Reserve */
GPIO(LED2_PWM,	PIN(0153), GPIO_OUT_LOW) /* Reserve */
GPIO(LED3_PWM,	PIN(0226), GPIO_OUT_LOW) /* Reserve */

GPIO(BATT_LOW_LED_L,    PIN(035),  GPIO_OUT_HIGH)
GPIO(BATT_CHG_LED_L,    PIN(0134), GPIO_OUT_HIGH)
GPIO(PWR_LED_L,         PIN(0133), GPIO_OUT_HIGH)

/* Alternate functions GPIO definitions */

/* KB pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)


/*
 * GPIO_0104(UART0_TX) Func1
 * GPIO_0105(UART0_RX) Func1
 * Bank 2 bits[4:5]
 */
ALTERNATE(PIN_MASK(2, 0x30), 1, MODULE_UART, 0)

/* KB pins */
/*
 * MEC1704H (144 pin package)
 * KSO00 = GPIO_0040 Func3 bank 1 bit 0
 * KSO01 = GPIO_0045 Func3 bank 1 bit 5
 * KSO02 = GPIO_0046 Func3 bank 1 bit 6
 * KSO03 = GPIO_0047 Func3 bank 1 bit 7
 * KSO04 = GPIO_0107 Func3 bank 2 bit 7
 * KSO05 = GPIO_0112 Func3 bank 2 bit 10
 * KSO06 = GPIO_0113 Func3 bank 2 bit 11
 * KSO07 = GPIO_0120 Func3 bank 2 bit 16
 * KSO08 = GPIO_0121 Func3 bank 2 bit 17
 * KSO09 = GPIO_0122 Func3 bank 2 bit 18
 * KSO10 = GPIO_0123 Func3 bank 2 bit 19
 * KSO11 = GPIO_0124 Func3 bank 2 bit 20
 * KSO12 = GPIO_0125 Func3 bank 2 bit 21
 * For 8x16 test keyboard add KSO13 - KSO15
 * KSO13 = GPIO_0126 Func3 bank 2 bit 22
 * KSO14 = GPIO_0132 Func3 bank 2 bit 26
 * KSO15 = GPIO_0151 Func3 bank 3 bit 9
 *
 * KSI0 = GPIO_0017 Func3 bank 0 bit 15
 * KSI1 = GPIO_0020 Func3 bank 0 bit 16
 * KSI2 = GPIO_0021 Func3 bank 0 bit 17
 * KSI3 = GPIO_0026 Func3 bank 0 bit 22
 * KSI4 = GPIO_0027 Func3 bank 0 bit 23
 * KSI5 = GPIO_0030 Func3 bank 0 bit 24
 * KSI6 = GPIO_0031 Func3 bank 0 bit 25
 * KSI7 = GPIO_0032 Func3 bank 0 bit 26
 */
/* KSI 0-7, Bank 0, Func3, bits 15-17, 22-26 */
ALTERNATE(PIN_MASK(0, 0x07C38000),	3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
/* KSO 0-3 Bank 1, Func3, bits 0, 5-7 */
ALTERNATE(PIN_MASK(1, 0xE1), 		3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
/* KSO 4-12, Bank 2, Func3, bits 7, 10-11, 16-21 */
ALTERNATE(PIN_MASK(2, 0x003F0C80),	3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
/* KSO 13-14, Bank 2, Func3, bits 22, 26 */
ALTERNATE(PIN_MASK(2, 0x04400000),	3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
/* KSO 15, Bank 3, Func3, bit 9 */
ALTERNATE(PIN_MASK(3, 0x00000200),	3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)

/* eSPI pins */
/* ESPI_RESET# - GPIO_0061 Function 2, Bank 1 bit[17] */
ALTERNATE(PIN_MASK(1, 0x20000), 2, MODULE_LPC, 0)
/* ESPI_ALERT# - GPIO_0063 Function 2, Bank 1 bit[19] */
ALTERNATE(PIN_MASK(1, 0x80000), 2, MODULE_LPC, 0)
/* ESPI_CS# - GPIO_0066 Function 2, Bank 1 bit[22] */
ALTERNATE(PIN_MASK(1, 0x400000), 2, MODULE_LPC, 0)
/* ESPI_CLK - GPIO_0065 Function 2, Bank 1 bit[21] */
ALTERNATE(PIN_MASK(1, 0x200000), 2, MODULE_LPC, 0)
/* ESPI_IO{0,1,2,3} - GPIO_0070-GPIO_0073 Function 2, Bank 1 bits[24:27] */
ALTERNATE(PIN_MASK(1, 0xf000000), 2, MODULE_LPC, 0)

/* MECC card SPI flash is connected to QMSPI Shared Port
 * Also, MEC1701H Private SPI Port (Port 1) has pins multiplexed
 * with KSO pins used in key scan!
 * QMSPI Shared SPI Port (Port 0)
 * NOTE: QMSPI Shared SPI Port pins are on VTR2
 */
/*
 * MEC1701H SHD SPI is connected to QMSPI controller.
 * QMSPI drives chip select. SHD_CS0#(GPIO_0055) must be set
 * to alternate function 2 and GPIO_ODR_HIGH.
 */
/* GPIO_0055 Function 2, Bank 1 bit[13] */
ALTERNATE(PIN_MASK(1, 0x2000), 2, MODULE_SPI_FLASH, GPIO_ODR_HIGH)
/* SHD_CLK - GPIO_0056 Function 2, Bank 1 bit[14] */
ALTERNATE(PIN_MASK(1, 0x4000), 2, MODULE_SPI_FLASH, 0)
/* MOSI(SHD_IO0) - GPIO_0223 Function 2, Bank 4 bit[19] */
/* MISO(SHD_IO1) - GPIO_0224 Function 2, Bank 4 bit[20] */
/* nWP(SHD_IO2) - GPIO_0227 Function 2, Bank 4 bit[23] */
ALTERNATE(PIN_MASK(4, 0x980000), 2, MODULE_SPI_FLASH, 0)
/* nHOLD(SHD_IO3) - GPIO_0016 Function 2, Bank 0 bit[14] */
ALTERNATE(PIN_MASK(0, 0x4000), 2, MODULE_SPI_FLASH, 0)

/*
 * MEC1701H GP-SPI0 Master
 * SPI0_CS# = GPIO_0003 Func 0(GPIO) Bank 0, bit 3
 * SPI0_CLK = GPIO_0034 Func 2	Bank 0, bit 28
 * SPI0_MISO = GPIO_0036 Func 2	Bank 0, bit 30
 * SPI0_MOSI = GPIO_0004 Func 2	Bank 0, bit 4
 */
ALTERNATE(PIN_MASK(0,  0x00000008), 0, MODULE_SPI_MASTER, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(0,  0x50000010), 2, MODULE_SPI_MASTER, 0)

/* I2C pins */
/* Using SMB00 as function 2 GP-SPI0
 * SMB01_DAT - GPIO_0005 Func1, SMB01_CLK - GPIO_0006 Func1
 * SMB03_DAT - GPIO_0007 Func1, SMB03_CLK - GPIO_0010 Func1
 * Bank 0 bits[5:8]
*/
ALTERNATE(PIN_MASK(0,  0x01E0), 1, MODULE_I2C, GPIO_ODR_HIGH)

/*
 * SMB05_DAT - GPIO_0141 Func1, SMB05_CLK - GPIO_0142 Func1
 * SMB04_DAT - GPIO_0143 Func1, SMB04_CLK - GPIO_0144 Func1
 * SMB02_DAT - GPIO_0154 Func1, SMB02_CLK - GPIO_0155 Func1
 * Bank 3 bits[1:4,12:13]
*/
ALTERNATE(PIN_MASK(3,  0x0000), 1, MODULE_I2C, GPIO_ODR_HIGH)

/* ADC pins */
/* ADC00 - GPIO_0200 / DCHG_I,			Func1 Bank 4 bit[0]
 * ADC01 - GPIO_0201 / ADP_I,			Func1 Bank 4 bit[1]
 * ADC02 - GPIO_0202 / VCIN1_BATT_TEMP,	Func1 Bank 4 bit[2]
 */
ALTERNATE(PIN_MASK(4,  0x0007), 1, MODULE_ADC, GPIO_ANALOG)

/* LED pins */
/*
*	GPIO156, LED0_PWM, Func1, Bank 3 bit[14]
*	GPIO157, LED1_PWM, Func1, Bank 3 bit[15]
*	GPIO153, LED2_PWM, Func1, Bank 3 bit[11]
*	GPIO226, LED3_PWM, Func1, Bank 4 bit[22]
*/	
/* ALTERNATE(PIN_MASK(3, 0xc800), 1, MODULE_POWER_LED, 0) */
/* ALTERNATE(PIN_MASK(4, 0x400000), 1, MODULE_POWER_LED, 0) */

/* PWM*/
/*
*	GPIO053, FAN_PWM0,		Func1, Bank 1 bit[11]
*	GPIO054, GPIO54_PWM1,	Func1, Bank 1 bit[12]
*	GPIO001, GPIO01_PWM4,	Func1, Bank 0 bit[1]
*	GPIO015, EC_BEEP,		Func1, Bank 0 bit[13]
*/
ALTERNATE(PIN_MASK(0, 0x2002), 1, MODULE_PWM, 0)
ALTERNATE(PIN_MASK(1, 0x1800), 1, MODULE_PWM, 0)
