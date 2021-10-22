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

UNIMPLEMENTED(WP)
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(RSMRST_L_PGOOD)
UNIMPLEMENTED(PCH_RTCRST)
UNIMPLEMENTED(SYS_RESET_L)
/* Interrupt Pin Defination */

/* Power Sequence */
GPIO_INT(ON_OFF_BTN_L,    PIN(0162), GPIO_INT_BOTH, mainboard_power_button_interrupt)
GPIO_INT(ON_OFF_FP_L,     PIN(0163), GPIO_INT_BOTH, fingerprint_power_button_interrupt)
GPIO_INT(PWR_3V5V_PG,     PIN(051),  GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_SUS_L,       PIN(0221), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(VCCIN_AUX_VR_PG, PIN(043),  GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PM_SLP_S3_L,     PIN(022),  GPIO_INT_BOTH, power_signal_interrupt) /* eSPI signal */
GPIO_INT(PM_SLP_S4_L,     PIN(0226), GPIO_INT_BOTH, power_signal_interrupt) /* eSPI signal */
GPIO_INT(VR_PWRGD,        PIN(057),  GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SOC_ENBKL,       PIN(0165), GPIO_INT_BOTH, soc_signal_interrupt)
GPIO_INT(LID_SW_L,        PIN(0254), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(CHASSIS_OPEN,    PIN(0161), GPIO_INT_BOTH, chassis_control_interrupt) /* monitor C cover open */
GPIO_INT(ADP_IN,              PIN(0172), GPIO_INT_BOTH, extpower_interrupt)    /* Adaptor in from charger ACOK pin */

/* UART input */
GPIO_INT(UART0_RX,        PIN(0105), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, \
	 uart_deepsleep_interrupt)

/* Sensor */
GPIO_INT(SOC_TP_INT_L,    PIN(0242), GPIO_INT_FALLING, touchpad_interrupt)

/* Input signal */
GPIO(CAM_SW,              PIN(067),  GPIO_INPUT)    /* Monitor to close CAM PWR */

/* Reserve Pin Defination */
GPIO(EC_SMB_SDA0,         PIN(03),   GPIO_INPUT)    /* Reserved for HID */
GPIO(EC_SMB_CLK0,         PIN(04),   GPIO_INPUT)    /* Reserved for HID */
GPIO_INT(PLT_RST_L,       PIN(024),  GPIO_INT_BOTH, soc_plt_reset_interrupt)    /* CPU triggered Platform reset */
GPIO(SX_EXIT_HOLDOFF_L,   PIN(025),  GPIO_INPUT)    /* Reserved for S0ix state */
GPIO(EN_INVPWR,           PIN(062),  GPIO_INPUT)  /* DVT2 LCD 19V power enable */
GPIO(MIC_SW,              PIN(0171), GPIO_INPUT)    /* Reserved for DMIC function */
GPIO(SOC_EC_INT_L,        PIN(0246), GPIO_INPUT | GPIO_ODR_HIGH)    /* Reserved for HID */
GPIO(VS_ON,               PIN(0241), GPIO_OUT_HIGH)  /* 3.3V and 5V rails */
GPIO(FP_CTRL,             PIN(0245), GPIO_INPUT)    /* Reserved for fingerprint control */
GPIO(GPIO170_STRAP,       PIN(0170), GPIO_INPUT)

/* power sequence output pins */
/*
 * This pulldown should be removed and SLP_S0 should be enabled as a power
 * signal interrupt in future hardware followers. The signal is pulled up
 * in the SoC when the primary rails are on and/or ramping.
 * In order to not get interrupt storms there should be external logic
 * which makes this a true binary signal into the EC.
 */
GPIO(PBTN_OUT_L,            PIN(0243),  GPIO_OUT_LOW)
GPIO(AC_PRESENT_OUT,        PIN(0240),  GPIO_ODR_LOW)
GPIO(EC_RSMRST_L,           PIN(036),   GPIO_INPUT | GPIO_OUT_LOW)
GPIO(EC_ON,                 PIN(0250),  GPIO_OUT_HIGH) /* keep +3VL_EC to power on */
GPIO(PCH_DPWROK,            PIN(012),   GPIO_ODR_LOW)
GPIO(PCH_PWR_EN,            PIN(0244),  GPIO_INPUT | GPIO_ODR_LOW)
GPIO(SYSON,                 PIN(0222),  GPIO_OUT_LOW)
GPIO(SUSP_L,                PIN(034),   GPIO_OUT_LOW)
GPIO(EC_VCCST_PG,           PIN(023),   GPIO_OUT_LOW)
GPIO(VR_ON,                 PIN(033),   GPIO_OUT_LOW)
GPIO(PCH_PWROK,             PIN(052),   GPIO_OUT_LOW)
GPIO(SYS_PWROK,             PIN(0106),  GPIO_OUT_LOW)


/* I2C pins - these will be reconfigured for alternate function below */
/* Batt CHarger */
GPIO(EC_SMB_CLK1, PIN(0131), GPIO_INPUT)
GPIO(EC_SMB_SDA1, PIN(0130), GPIO_INPUT)
/* Touchpad */
GPIO(EC_I2C_3_SCL, PIN(0155),  GPIO_INPUT)
GPIO_INT(EC_I2C_3_SDA, PIN(0154),  GPIO_INT_FALLING, touchpad_i2c_interrupt)
/* Therm Sensor */
GPIO(EC_SMB_CLK3, PIN(010), GPIO_INPUT)
GPIO(EC_SMB_SDA3, PIN(07), GPIO_INPUT)
/* PD */
GPIO(EC_I2C06_PD_CLK, PIN(0140),  GPIO_INPUT)
GPIO(EC_I2C06_PD_SDA, PIN(0132),  GPIO_INPUT)


/* PD controller */
GPIO_INT(EC_PD_INTA_L,    PIN(0115), GPIO_INT_FALLING, pd0_chip_interrupt)
GPIO_INT(EC_PD_INTB_L,    PIN(0127), GPIO_INT_FALLING, pd1_chip_interrupt)
GPIO(AC_PRESENT_PD_L,     PIN(013),  GPIO_INPUT)
GPIO_INT(TYPEC0_VBUS_ON_EC,     PIN(0144), GPIO_INT_BOTH | GPIO_ODR_HIGH, pd_extpower_is_present_interrupt)   /* Type-C VBUS select */
GPIO_INT(TYPEC1_VBUS_ON_EC,     PIN(0143), GPIO_INT_BOTH | GPIO_ODR_HIGH, pd_extpower_is_present_interrupt)   /* Type-C VBUS select */
GPIO_INT(TYPEC2_VBUS_ON_EC,     PIN(0142), GPIO_INT_BOTH | GPIO_ODR_HIGH, pd_extpower_is_present_interrupt)   /* Type-C VBUS select */
GPIO_INT(TYPEC3_VBUS_ON_EC,     PIN(0141), GPIO_INT_BOTH | GPIO_ODR_HIGH, pd_extpower_is_present_interrupt)   /* Type-C VBUS select */
GPIO(EC_PROCHOT_L,          PIN(0206), GPIO_INPUT | GPIO_ODR_HIGH)

/* Fan PWM output and TACH input. PROCHOT input */
/* MEC1701H GPIO_0050/FAN_TACH0 OK */
GPIO(FAN_SPEED,         PIN(050), GPIO_INPUT)


/* Miscellaneous */
GPIO(TYPEC_G_DRV2_EN,       PIN(0101), GPIO_OUT_HIGH)    /* LED drv */
GPIO(ME_EN,                 PIN(060),  GPIO_OUT_LOW)	/* DVT ME control use VTR1(3.3v) */
GPIO(ME_EN_PVT,             PIN(064),  GPIO_OUT_LOW)	/* PVT ME control use VTR3(1.8v) */
GPIO(EC_BKOFF_L,            PIN(0102), GPIO_OUT_LOW)    /* Panel backlight control */

GPIO(CAM_EN,                PIN(0255), GPIO_OUT_LOW)    /* Open camera power  */
/* GPIO(FAN_PWM0,              PIN(053),  GPIO_OUT_HIGH) */   /* EVT for fan full on */
GPIO(EC_MUTE_L,             PIN(0253), GPIO_ODR_LOW)
GPIO(OTG_EN,                PIN(0207), GPIO_OUT_LOW)	/* Always low PWR request */

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
 * TODO(crosbug.com/p/40848): These LEDs should be under control of the
 * mec1701 LED control unit. Remove these GPIO definitions once the LED
 * control unit is functional.
 */
/*GPIO(FPR_LED_R,	PIN(0156), GPIO_OUT_LOW)*/    /* TODO: implement PWM */
/*GPIO(FPR_LED_G,	PIN(0157), GPIO_OUT_LOW)*/    /* TODO: implement PWM */
/*GPIO(FPR_LED_B,	PIN(0153), GPIO_OUT_LOW)*/    /* TODO: implement PWM */

GPIO(CAP_LED_L,     PIN(0114), GPIO_OUT_LOW)

/*
 * Analog pins
 */
GPIO(TP_BOARD_ID, PIN(0203),  GPIO_FLAG_NONE)
GPIO(AD_BOARD_ID, PIN(0205),  GPIO_FLAG_NONE)



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

/* KSI 0-7,   Bank 0, Func1, bits 15-17, 22-26 */
ALTERNATE(PIN_MASK(0, 0x07C38000),	1, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
/* KSO 0,     Bank 1, Func2, bit 0 */
/* KSO 1-3,   Bank 1, Func1, bit 5-7 */
ALTERNATE(PIN_MASK(1, 0x00000001),	2, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1, 0x000000E0),	1, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
/* KSO 4,     Bank 2, Func2, bits 7 */
/* KSO 5-7,   Bank 2, Func1, bits 10-11, 16 */
/* KSO 8-13,  Bank 2, Func2, bits 17-22 */
ALTERNATE(PIN_MASK(2, 0x00000080),	2, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(2, 0x00010C00),	1, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(2, 0x007E0000),	2, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
/* KSO 14, Bank 3, Func1, bit 10 */
/* KSO 15, Bank 3, Func2, bit 9 */
ALTERNATE(PIN_MASK(3, 0x00000400),	1, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(3, 0x00000200),	2, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)

/* eSPI pins */
/* ESPI_RESET# - GPIO_0061 Function 1, Bank 1 bit[17] */
ALTERNATE(PIN_MASK(1, 0x20000), 1, MODULE_LPC, 0)
/* ESPI_ALERT# - GPIO_0063 Function 1, Bank 1 bit[19] */
ALTERNATE(PIN_MASK(1, 0x80000), 1, MODULE_LPC, 0)
/* ESPI_CS# - GPIO_0066 Function 1, Bank 1 bit[22] */
ALTERNATE(PIN_MASK(1, 0x400000), 1, MODULE_LPC, 0)
/* ESPI_CLK - GPIO_0065 Function 1, Bank 1 bit[21] */
ALTERNATE(PIN_MASK(1, 0x200000), 1, MODULE_LPC, 0)
/* ESPI_IO{0,1,2,3} - GPIO_0070-GPIO_0073 Function 1, Bank 1 bits[24:27] */
ALTERNATE(PIN_MASK(1, 0xf000000), 1, MODULE_LPC, 0)

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
/* MOSI(SHD_IO0) - GPIO_0223 Function 1, Bank 4 bit[19] */
/* MISO(SHD_IO1) - GPIO_0224 Function 2, Bank 4 bit[20] */
/* nWP(SHD_IO2) - GPIO_0227 Function 1, Bank 4 bit[23] */
ALTERNATE(PIN_MASK(4, 0x880000), 1, MODULE_SPI_FLASH, 0)
ALTERNATE(PIN_MASK(4, 0x100000), 2, MODULE_SPI_FLASH, 0)
/* nHOLD(SHD_IO3) - GPIO_0016 Function 2, Bank 0 bit[14] */
ALTERNATE(PIN_MASK(0, 0x4000), 2, MODULE_SPI_FLASH, 0)

/* I2C pins */

/* SMB00_DAT - GPIO_0003 Func1, SMB00_CLK - GPIO_0004 Func1 (bank 0 bits[3:4], reserved)
 * SMB01_DAT - GPIO_0130 Func1, SMB01_CLK - GPIO_0131 Func1 (bank 2 bits[24:25])
 * SMB02_DAT - GPIO_0154 Func1, SMB02_CLK - GPIO_0155 Func1 (bank 3 bits[12:13])
 * SMB03_DAT - GPIO_0007 Func1, SMB03_CLK - GPIO_0010 Func1 (bank 0 bits[7:8])
 * SMB06_DAT - GPIO_0132 Func1, SMB06_CLK - GPIO_0140 Func1 (bank 2 bit[26], bank 3 bit[0])
*/
ALTERNATE(PIN_MASK(0, 0x0198),    1, MODULE_I2C, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(2, 0x7000000), 1, MODULE_I2C, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(3, 0x3001),    1, MODULE_I2C, GPIO_ODR_HIGH)

/* ADC pins */
/* ADC00 - GPIO_0200 / I_ADP,			Func1 Bank 4 bit[0]
 * ADC01 - GPIO_0201 / I_SYS,			Func1 Bank 4 bit[1]
 * ADC02 - GPIO_0202 / VCIN1_BATT_TEMP,	Func1 Bank 4 bit[2]
 * ADC03 - GPIO_0203 / TP_BOARD_ID,		Func1 Bank 4 bit[3]
 * ADC04 - GPIO_0204 / AD_BID,			Func1 Bank 4 bit[4]
 * ADC05 - GPIO_0205 / AUDIO_BOARD_ID,	Func1 Bank 4 bit[5]
 */
ALTERNATE(PIN_MASK(4,  0x003F), 1, MODULE_ADC, GPIO_ANALOG)

/* LED pins */
/*
*	GPIO156, LED0_PWM, Func1, Bank 3 bit[14]
*	GPIO157, LED1_PWM, Func1, Bank 3 bit[15]
*	GPIO153, LED2_PWM, Func1, Bank 3 bit[11]
*/	
ALTERNATE(PIN_MASK(3, 0xc800), 1, MODULE_PWM, 0)

/* PWM*/
/*
 *	GPIO053,  FAN_PWM0,		Func1, Bank 1 bit[11]
 */
ALTERNATE(PIN_MASK(1, 0x800), 1, MODULE_PWM, GPIO_ODR_HIGH)
 /*
 *	GPIO054,  TYPEC_R_DRV2,		Func1, Bank 1 bit[12]
 */
ALTERNATE(PIN_MASK(1, 0x1000), 1, MODULE_PWM, 0)

/*
 *  EVT-GPIO011,  TYPEC_G_DRV2,		Func2, Bank 0 bit[9]
 *	DVT-GPIO011,  EC_KBL_PWR_EN,	Func2, Bank 0 bit[9]
 */
ALTERNATE(PIN_MASK(0, 0x0200), 2, MODULE_PWM, 0)
/*
 *	GPIO02,   TYPEC_R_DRV1,		Func1, Bank 0 bit[2]
 *	GPIO014,  TYPEC_G_DRV1,		Func1, Bank 0 bit[12]
 *	GPIO015,  TYPEC_B_DRV1,		Func1, Bank 0 bit[13]
 *	GPIO035,  TYPEC_B_DRV2,		Func1, Bank 0 bit[29]

 *  GPIO0175, EC_BEEP,
 */
ALTERNATE(PIN_MASK(0, 0x20003004), 1, MODULE_PWM, 0)

ALTERNATE(PIN_MASK(1, 0x100), 1, MODULE_PWM, 0)

/* SHD_CLK - GPIO_0056 Function 2, Bank 1 bit[14]
 * TYPEC_G_DRV2 - GPIO_0056 Function 1, Bank 1 bit[14]
 * in DVT normal enable to PWM Module, need flash  switch to SHD_CLK.
 */
ALTERNATE(PIN_MASK(1, 0x4000), 1, MODULE_PWM, 0)

/* PECI
 *  GPIO042,  H_PECI,       Func1, Bank 1 bit[2]
 *  GPIO044,  VREF_VTT,     Func1, Bank 1 bit[4]
 */
ALTERNATE(PIN_MASK(1, 0x0014), 1, MODULE_PECI, 0)
