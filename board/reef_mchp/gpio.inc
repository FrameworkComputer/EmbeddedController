/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* MEC1701H GPIO_0105/UART0_RX OK */
GPIO_INT(UART0_RX, PIN(0105), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, \
	 uart_deepsleep_interrupt)

GPIO_INT(PCH_PLTRST_L, PIN(064), GPIO_INT_BOTH | GPIO_PULL_UP, lpcrst_interrupt)

GPIO_INT(CHARGER_INT_L,	PIN(0143), GPIO_INT_FALLING, bd9995x_vbus_interrupt)
/* CHARGER_EC_INT_ODL from BD99956 */
/*
 * TODO: The pull ups for Parade TCPC interrupt line can be removed in versions
 * of board following EVT in which daughter card (which has an external pull up)
 * will always be inserted.
 */
GPIO_INT(USB_C0_PD_INT_ODL, PIN(0175), GPIO_INT_FALLING, tcpc_alert_event)
/* from Analogix TCPC */
GPIO_INT(USB_C1_PD_INT_ODL, PIN(0126), GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)
/* from Parade TCPC */

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
GPIO_INT(USB_C0_CABLE_DET, PIN(0246), GPIO_INT_RISING, anx74xx_cable_det_interrupt)
/* CABLE_DET from ANX3429 */
#endif

#ifdef CONFIG_POWER_S0IX
GPIO_INT(PCH_SLP_S0_L, PIN(050), GPIO_INT_BOTH, power_signal_interrupt) /* SLP_S0_L */
#endif
GPIO_INT(PCH_SLP_S4_L, PIN(033), GPIO_INT_BOTH, power_signal_interrupt)		/* SLP_S4_L */
GPIO_INT(PCH_SLP_S3_L, PIN(035), GPIO_INT_BOTH, power_signal_interrupt)		/* SLP_S3_L */
GPIO_INT(SUSPWRDNACK,   PIN(057), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(RSMRST_L_PGOOD, PIN(0243), GPIO_INT_BOTH, power_signal_interrupt)		/* PMIC_EC_RSMRST_ODL */
GPIO_INT(ALL_SYS_PGOOD,	PIN(0240), GPIO_INT_BOTH, power_signal_interrupt)		/* PMIC_EC_PWROK_OD */

/* ACOK_OD from BD99956 */
GPIO_INT(AC_PRESENT, PIN(0242), GPIO_INT_BOTH, extpower_interrupt)

/* TODO: We might remove external pull-up for POWER_BUTTON_L in EVT */
GPIO_INT(POWER_BUTTON_L, PIN(0241), GPIO_INT_BOTH, power_button_interrupt)		/* MECH_PWR_BTN_ODL */
GPIO_INT(LID_OPEN, PIN(0206), GPIO_INT_BOTH, lid_interrupt)
/* Volume up and down buttons need to be swapped. The one closer to the hinge
 * should be volume up and the one closer to the user should be volume down.
 * (cros.bug/p/60057) */
GPIO_INT(EC_VOLDN_BTN_ODL_SWAPPED,	PIN(042), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_VOLUP_BTN_ODL_SWAPPED,	PIN(044), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
#define GPIO_EC_VOLDN_BTN_ODL GPIO_EC_VOLUP_BTN_ODL_SWAPPED
#define GPIO_EC_VOLUP_BTN_ODL GPIO_EC_VOLDN_BTN_ODL_SWAPPED
/* Tablet switch is active-low. L: lid is attached (360 position) H: detached */
GPIO_INT(TABLET_MODE_L,		PIN(0207), GPIO_INT_BOTH, tablet_mode_interrupt)

GPIO_INT(WP_L,			PIN(0152), GPIO_INT_BOTH | GPIO_SEL_1P8V, switch_interrupt)	/* EC_WP_ODL */

GPIO_INT(BASE_SIXAXIS_INT_L,	PIN(0131), GPIO_INT_FALLING | GPIO_SEL_1P8V,
	 bmi160_interrupt)
GPIO(LID_ACCEL_INT_L,		PIN(014), GPIO_INPUT | GPIO_SEL_1P8V)

/* I2C GPIOs will be set to alt. function later. */
GPIO(EC_I2C_GYRO_SDA,		PIN(0132), GPIO_INPUT) /* original reef | GPIO_SEL_1P8V */
GPIO(EC_I2C_GYRO_SCL,		PIN(0140), GPIO_INPUT) /* original reef | GPIO_SEL_1P8V */
GPIO(EC_I2C_SENSOR_SDA,		PIN(012), GPIO_INPUT) /* original reef | GPIO_SEL_1P8V */
GPIO(EC_I2C_SENSOR_SCL,		PIN(013), GPIO_INPUT) /* original reef | GPIO_SEL_1P8V */
GPIO(EC_I2C_USB_C0_PD_SDA,	PIN(003), GPIO_INPUT)
GPIO(EC_I2C_USB_C0_PD_SCL,	PIN(004), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_PD_SDA,	PIN(0154), GPIO_INPUT)
GPIO(EC_I2C_USB_C1_PD_SCL,	PIN(0155), GPIO_INPUT)
GPIO(EC_I2C_POWER_SDA,		PIN(007), GPIO_INPUT)
GPIO(EC_I2C_POWER_SCL,		PIN(010), GPIO_INPUT)

/*
 * LPC:
 * Pins 46, 47, 51, 52, 53, 54, 55, default to LPC mode.
 * Pin 56 (CLKRUN#) defaults to GPIO mode.
 * Pin 57 (SER_IRQ) defaults to LPC mode, but we also have EC_PCH_KB_INT_ODL
 * (Pin B0) in case it doesn't work (Set CONFIG_KEYBOARD_IRQ_GPIO in this case).
 *
 * See also the NO_LPC_ESPI bit in DEVALT1 and the CONFIG_HOSTCMD_SPS option.
 */

GPIO(PCH_SMI_L,			PIN(0227), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_SMI_ODL */
GPIO(PCH_SCI_L,			PIN(0222), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_SCI_ODL */
#ifndef CONFIG_POWER_S0IX
GPIO(PCH_SLP_S0_L,              PIN(050), GPIO_INPUT)				/* SLP_S0_L */
#endif

/*
 * BRD_ID1 is a an ADC pin which will be used to measure multiple values.
 * Assert EC_BRD_ID_EN_ODL and then read BRD_ID1.
 */
ALTERNATE(PIN_MASK(4, 0x04), 1, MODULE_ADC, 0) /* GPIO 0202 bank=4 bit=2 func=1 ADC02 */
GPIO(EC_BRD_ID_EN_ODL,		PIN(0221), GPIO_INPUT)

GPIO(CCD_MODE_ODL,		PIN(0203), GPIO_INPUT)
GPIO(EC_HAVEN_RESET_ODL,	PIN(034), GPIO_ODR_HIGH)
GPIO(ENTERING_RW,		PIN(0254), GPIO_OUTPUT)			/* EC_ENTERING_RW */

GPIO(PCH_RSMRST_L,		PIN(0165), GPIO_OUT_LOW | GPIO_PULL_UP)
GPIO(EC_BATT_PRES_L,		PIN(0204), GPIO_INPUT)
GPIO(PMIC_EN,			PIN(0245), GPIO_OUT_LOW)
GPIO(EN_PP3300,			PIN(023), GPIO_OUT_LOW)
GPIO(PP3300_PG,			PIN(0156), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EN_PP5000,			PIN(051), GPIO_OUT_LOW)
GPIO(PP5000_PG,			PIN(0157), GPIO_INPUT | GPIO_PULL_UP)
GPIO(EN_P3300_TRACKPAD_ODL,	PIN(043), GPIO_ODR_LOW)
/* Control the gate for trackpad IRQ. High closes the gate.
 * This is always set low so that the OS can manage the trackpad. */
GPIO(TRACKPAD_INT_GATE,		PIN(054), GPIO_OUT_LOW)
GPIO(PCH_SYS_PWROK,		PIN(0106), GPIO_OUT_LOW)	/* EC_PCH_PWROK */
GPIO(ENABLE_BACKLIGHT,		PIN(002), GPIO_ODR_HIGH | GPIO_SEL_1P8V)	/* EC_BL_EN_OD */

GPIO(WIRELESS_GPIO_WLAN_POWER,	PIN(0142), GPIO_ODR_HIGH)		/* EN_PP3300_WLAN_ODL */

/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,		PIN(0114), GPIO_INPUT | GPIO_SEL_1P8V)	/* PCH_PROCHOT_ODL */

GPIO(PCH_PWRBTN_L,		PIN(0244), GPIO_ODR_HIGH)		/* EC_PCH_PWR_BTN_ODL */
GPIO(PCH_WAKE_L,		PIN(0115), GPIO_ODR_HIGH)		/* EC_PCH_WAKE_ODL */
GPIO(USB_C0_HPD_1P8_ODL,	PIN(052), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(USB_C1_HPD_1P8_ODL,	PIN(0151), GPIO_INPUT | GPIO_SEL_1P8V)

GPIO(USB2_OTG_VBUSSENSE,	PIN(053), GPIO_OUTPUT)

/* EC_PCH_RTCRST is a sledgehammer for resetting SoC state and should rarely
 * be used. Set as input for now, we'll set it as an output when we want to use
 * it. Has external pull-down resistor. */
GPIO(EC_PCH_RTCRST,		PIN(0205), GPIO_INPUT)
/* Latest code (2018-03-28) in power/intel_x86.c uses SYS_RESET_L signal name
 * Previous Reef used PCH_RCIN_L
 */
GPIO(SYS_RESET_L,		PIN(036), GPIO_ODR_HIGH) /* SYS_RST_ODL */

/* FIXME: What, if anything, to do about EC_RST_ODL on VCC1_RST#? */

GPIO(CHARGER_RST_ODL,		PIN(0141), GPIO_ODR_HIGH)
GPIO(USB_A_CHARGE_EN_L,		PIN(022), GPIO_OUT_LOW)
GPIO(EN_USB_TCPC_PWR,		PIN(0100), GPIO_OUT_LOW)
GPIO(USB1_ENABLE,		PIN(0144), GPIO_OUT_LOW)

GPIO(USB_C0_PD_RST_L,		PIN(024), GPIO_OUT_LOW)		/* USB_C0_PD_RST_L */
GPIO(USB_C1_PD_RST_ODL,		PIN(0127), GPIO_ODR_LOW)

/*
 * Configure as input to enable @ 1.5A, output-low to turn off, or output-high
 * to enable @ 3A.
 */
GPIO(USB_C0_5V_EN,		PIN(011), GPIO_OUT_LOW | GPIO_PULL_UP)	/* EN_USB_C0_5V_OUT, Enable C0 */
GPIO(USB_C1_5V_EN,		PIN(061), GPIO_OUT_LOW | GPIO_PULL_UP)	/* EN_USB_C1_5V_OUT, Enable C1 */

/* Clear for non-HDI breakout, must be pulled high */
/* reefmchp board note: following 3 GPIOs are not NC on reef board, which are not mapping to MEC1701 */
//GPIO(NC1,			PIN(0154), GPIO_INPUT | GPIO_PULL_UP | GPIO_SEL_1P8V)
//GPIO(NC2,			PIN(0155), GPIO_INPUT | GPIO_PULL_UP | GPIO_SEL_1P8V)

//GPIO(ENG_STRAP,			PIN(0156), GPIO_INPUT)

GPIO(BAT_LED_BLUE,		PIN(0153), GPIO_OUT_HIGH)
GPIO(BAT_LED_AMBER,		PIN(0226), GPIO_OUT_HIGH)

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
GPIO(QMSPI_CS0,	PIN(055),  GPIO_ODR_HIGH)

/*
 * Alternate function pins
 */

/*
 * MEC1701H SHD SPI is connected to QMSPI controller.
 * QMSPI drives chip select. SHD_CS0#(GPIO_0055) must be set
 * to alternate function 2 and GPIO_ODR_HIGH.
 * GPIO_0055 Function 2, Bank 1 bit[13]
 */
ALTERNATE(PIN_MASK(1, 0x2000), 2, MODULE_SPI_FLASH, GPIO_ODR_HIGH)
/* SHD_CLK - GPIO_0056 Function 2, Bank 1 bit[14] */
ALTERNATE(PIN_MASK(1, 0x4000), 2, MODULE_SPI_FLASH, 0)
/* MOSI(SHD_IO0) - GPIO_0223 Function 2, Bank 4 bit[19] */
/* MISO(SHD_IO1) - GPIO_0224 Function 2, Bank 4 bit[20] */
ALTERNATE(PIN_MASK(4, 0x180000), 2, MODULE_SPI_FLASH, 0)

/* MEC1701H LPC all alternate function 1
 *			bank	bit
 * GPIO061 LPCPD#	1	17
 * GPIO063 SER_IRQ	1	19
 * GPIO064 LRESET#	1	20 need internal pull-up
 * GPIO065 PCI_CLK	1	21
 * GPIO066 LFRAME#	1	22
 * GPIO067 CLKRUN#	1	23
 * GPIO070 LAD0		1	24
 * GPIO071 LAD1		1	25
 * GPIO072 LAD2		1	26
 * GPIO073 LAD3		1	27
 */
ALTERNATE(PIN_MASK(1, 0x0FF80000), 1, MODULE_LPC, 0)
ALTERNATE(PIN_MASK(1, 0x00100000), 1, MODULE_LPC, GPIO_PULL_UP | GPIO_INT_BOTH)

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
#define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_LOW)

/*
 * 8 x 13 key scan matrix
 * MEC1704H (144 pin package)
 *
 * KSI0 = GPIO_0017 Func3 bank 0 bit 15
 * KSI1 = GPIO_0020 Func3 bank 0 bit 16
 * KSI2 = GPIO_0021 Func3 bank 0 bit 17
 * KSI3 = GPIO_0026 Func3 bank 0 bit 22
 * KSI4 = GPIO_0027 Func3 bank 0 bit 23
 * KSI5 = GPIO_0030 Func3 bank 0 bit 24
 * KSI6 = GPIO_0031 Func3 bank 0 bit 25
 * KSI7 = GPIO_0032 Func3 bank 0 bit 26
 *
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
 */
/* KSI 0-7, Bank 0, Func3, bits 15-17, 22-26 */
ALTERNATE(PIN_MASK(0, 0x07C38000), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
/* KSO 4-12, Bank 2, Func3, bits 7, 10-11, 16-21 */
ALTERNATE(PIN_MASK(2, 0x003F0C80), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)

#ifdef CONFIG_KEYBOARD_COL2_INVERTED
/* KSO 0,1,3 Bank 1, Func3, bits 0, 5-7 */
ALTERNATE(PIN_MASK(1, 0xA1), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
GPIO(KBD_KSO2, PIN(046), GPIO_KB_OUTPUT_COL2)
#else
/* KSO 0-3 Bank 1, Func3, bits 0, 5-7 */
ALTERNATE(PIN_MASK(1, 0xE1), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
#endif


/* MEC1701H ADC01=GPIO201 Func1, ADC00=GPIO200 Func 1
 * Bank 1 bits[1:0]
 * TEMP_SENSOR_AMB (GPIO201) and TEMP_SENSOR_CHARGER (GPIO200)
 */
ALTERNATE(PIN_MASK(4, 0x03), 1, MODULE_ADC, 0)

/* MEC1701H implements I2C[0,2,3,6,7]
 * We need 5 ports.
 * I2C00_SDA is GPIO003 Func1, I2C00_SCL is GPIO004 Func1 bank 0 bits[3:4]
 * I2C03_SDA is GPIO007 Func1, I2C03_SCL is GPIO010 Func1 bank 0 bits[7:8]
 * I2C07_SDA is GPIO012 Func1, I2C07_SCL is GPIO013 Func1 bank 0 bits[10:11]
 * I2C06_SDA is GPIO132 Func1, I2C06_SCL is GPIO140 Func1 bank 2 bits[26]; bank 3 bits[0]
 * I2C02_SDA is GPIO154 Func1, I2C02_SCL is GPIO155 Func1 bank 3 bits[12:13]
 */
/* GPIO003-004 for EC_I2C_USB_C0_PD_SDA/SCL */
/* GPIO007-010 for EC_I2C_POWER_SDA/SCL */
/* GPIO012-013 for EC_I2C_SENSOR_SDA/SCL */
ALTERNATE(PIN_MASK(0, 0x00000D98), 1, MODULE_I2C, 0)
/* GPIO132 for EC_I2C_GYRO_SDA */
ALTERNATE(PIN_MASK(2, 0x04000000), 1, MODULE_I2C, 0)
/* GPIO140 for EC_I2C_GYRO_SCL */
/* GPIO154-155 for EC_I2C_USB_C1_PD_SDA/SCL */
ALTERNATE(PIN_MASK(3, 0x00003001), 1, MODULE_I2C, 0)

/*
 * PWM4 = GPIO001 Func1
 * PWM5 = GPIO002 Func1
 */
/* reefmchp board note: not PWM in reef or reedmchp boards design?? */
//ALTERNATE(PIN_MASK(0, 0x00000006), 1, MODULE_PWM, 0)

/*
 * GPIO_0104(UART0_TX) Func1
 * GPIO_0105(UART0_RX) Func1
 * Bank 2 bits[4:5]
 */
ALTERNATE(PIN_MASK(2, 0x30), 1, MODULE_UART, 0)

/*
 * MCHP TFDP alternate function configuration
 * GPIO 0170 = clock, 0171 = data both function 1
 * Port = 3 bits[24:25]
 */
ALTERNATE(PIN_MASK(3, 0x03000000), 1, MODULE_TFDP, 0)

/* reefmchp board note: specific for reedmchp board only */

/* GPIO101 - enable high accurate reference voltage regulator MAX6070
   1 - enable regulator, in normal runtime
   0 - disable regulator, in low power mode
 */
GPIO(VOL_REGULATOR_EN,	PIN(0101), GPIO_OUT_HIGH)

/* GPIO015 - enable 3.3V / 1.8V level switch TXS0108
   1 - enable level switch, in normal runtime
   0 - disable level switch, in low power mode
 */
GPIO(LEVEL_SW_EN,	PIN(015), GPIO_OUT_HIGH | GPIO_SEL_1P8V)

/* DEBUG I2C */
GPIO(GP025, PIN(025), GPIO_OUT_HIGH | GPIO_PULL_UP)

/* Low power deep sleep test. GPIO_0060 Function 2 48MHZ_OUT */
ALTERNATE(PIN_MASK(1, 0x10000), 2, MODULE_CHIPSET, 0)

/* Unused pins - on test header J47 */
/* Clear for non-HDI breakout, must be pulled high */
GPIO(NC1_UNUSED,    PIN(062), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC2_UNUSED,    PIN(0102), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC3_UNUSED,    PIN(000), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC4_UNUSED,    PIN(0130), GPIO_INPUT | GPIO_PULL_UP)
/* GPIO(NC5_UNUSED,    PIN(060), GPIO_INPUT | GPIO_PULL_UP) */
/* use this for low power mode test */
/* ALTERNATE(PIN_MASK(1, 0x00010000), 2, MODULE_GPIO, 0) */
GPIO(NC6_UNUSED,    PIN(0161), GPIO_INPUT | GPIO_PULL_UP)
/* GPIO(NC7_UNUSED,    PIN(025), GPIO_INPUT | GPIO_PULL_UP) */
GPIO(NC8_UNUSED,    PIN(0162), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC9_UNUSED,    PIN(0172), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC10_UNUSED,   PIN(0163), GPIO_INPUT | GPIO_PULL_UP)
