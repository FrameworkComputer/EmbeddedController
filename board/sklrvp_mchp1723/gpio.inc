/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * mec172x EVB board GPIO pins
 * !!!!!!!!!!!!!!!!!!!!!!! IMPORTANT !!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * MCHP MEC data sheet data sheet GPIO numbers are OCTAL.
 * Original glados MEC1322 used these octal numbers as base 10.
 * MCHP MEC and its boards will use OCTAL therefore make sure all
 * numbers used below are written as C OCTAL with a leading 0.
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 */
/* NOTE: MEC172x JTAG
 * GPIO_0145 is JTAG_TDI
 * GPIO_0146 is JTAG_TDO (SWV)
 * GPIO_0147 is JTAG_CLK (SWD_CLK)
 * GPIO_0150 is JTAG_TMS (SWD_IO)
 */
/* NOTE: MEC172x SZ(144 pin) and LJ(176 pin) do not implement:
 * 0037, 0077, 0136, 0163, 0164, 0167, 0176, 0177, 0237, 0247, 0251, 0252,
 * 0256, 0257
 * SZ 144-pin package does not implement:
 * 0001, 0005, 0006, 0041, 0110, 0111, 0133 - 0135, 0160, 0166, 0172 - 0174,
 * 0210 - 0217, 0220, 0225, 0230 - 0236, 0250, 0253
 */

/* include common gpio.inc under chip/mchp/lfw/... */
#include "chip/mchp/lfw/gpio.inc"

#define GPIO_BOTH_EDGES_PU (GPIO_INT_BOTH | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_LOW)
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)

/* MEC172x Host interfaces are eSPI, SPI peripheral, or I2C */

GPIO_INT(LID_OPEN, PIN(015), GPIO_INT_BOTH | GPIO_PULL_UP, lid_interrupt)


GPIO_INT(AC_PRESENT, PIN(014), GPIO_INT_BOTH, extpower_interrupt)

/* DC jack handled by intelrvp base board */
GPIO_INT(DC_JACK_PRESENT, PIN(0244), GPIO_INT_BOTH, board_dc_jack_interrupt)

/* Buffered power button input from PMIC / ROP_EC_PWR_BTN_L_R */
GPIO_INT(POWER_BUTTON_L, PIN(023), GPIO_INT_BOTH, power_button_interrupt)

/* RSMRST from PMIC */
GPIO_INT(RSMRST_L_PGOOD, PIN(0126), GPIO_INT_BOTH, power_signal_interrupt)

GPIO_INT(PCH_SLP_SUS_L, PIN(0175), GPIO_INT_BOTH, power_signal_interrupt)

GPIO_INT(PMIC_INT_L, PIN(0161), GPIO_INT_FALLING, power_signal_interrupt)

/* Delayed PWR_OK from PMIC */
GPIO_INT(PMIC_DPWROK, PIN(0151), GPIO_INT_BOTH, power_signal_interrupt)

/* Handle ALL_SYS_PWRGD from SKL RVP3 board */
GPIO_INT(ALL_SYS_PWRGD, PIN(057), GPIO_INT_BOTH, board_all_sys_pwrgd_interrupt)


GPIO_INT(TABLET_MODE_L, PIN(0100), GPIO_INT_BOTH | GPIO_PULL_UP, \
	 gmr_tablet_switch_isr)


/* UART0 RX input wake event */
GPIO_INT(UART0_RX, PIN(0105), GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP, \
	 uart_deepsleep_interrupt)

/*
 * NOTE: GPIO 0101 and 0102 are BGPO control by default. Chip level
 * GPIO code will switch these pins to GPIO mode. BGPO default state
 * is push-pull drive low.
 */
GPIO_INT(USB_C0_VBUS_INT, PIN(0101), GPIO_INT_BOTH, tcpc_alert_event)
GPIO_INT(USB_C1_VBUS_INT, PIN(0102), GPIO_INT_BOTH, tcpc_alert_event)

/*---- GPIO Pins not interrupt enabled ----*/

GPIO(VOLUME_UP_L,       PIN(034), GPIO_INPUT | GPIO_PULL_UP)
GPIO(VOLUME_DOWN_L,     PIN(035), GPIO_INPUT | GPIO_PULL_UP)
GPIO(WP_L,              PIN(036), GPIO_INPUT)

/* PCH and CPU pins */

GPIO(PCH_SLP_S0_L,          PIN(0222), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(PCH_PWRBTN_L,          PIN(043),  GPIO_OUTPUT)
GPIO(PCH_SEC_DISABLE_L,     PIN(022),  GPIO_OUT_HIGH)

GPIO(PCH_WAKE_L,            PIN(0255),  GPIO_ODR_HIGH)
GPIO(PCH_ACOK,              PIN(0246), GPIO_OUT_LOW)
GPIO(PCH_RSMRST_L,          PIN(012), GPIO_OUT_LOW)
GPIO(PCH_RTCRST,            PIN(052), GPIO_OUT_LOW)

GPIO(CPU_PROCHOT,           PIN(0171), GPIO_OUT_LOW)

GPIO(SYS_RESET_L,           PIN(025), GPIO_OUT_LOW)

GPIO(PMIC_SLP_SUS_L,        PIN(033), GPIO_OUT_LOW)

GPIO(EC_PCH_DSW_PWROK, PIN(013), GPIO_OUT_LOW)

GPIO(BATLOW_L_PMIC_LDO_EN,  PIN(0240),  GPIO_INPUT)

/*
 * I2C pins - these will be reconfigured for alternate function below
 * SZ package does not implement I2C11 pins.
 * Ports 0, 2 - 6, 9, 10, 12, 15  on VTR1 (3.3V only)
 * Ports 1, 7, 8 on VTR2 (auto-detect 3.3V or 1.8V)
 * Port 11 SDA on VBAT and SCL on VTR1
 * Ports 13, 14 on VTR3 (1.8V)
 * MEC172x has five I2C controllers. Assign one port per controller.
 */
GPIO(I2C00_SCL, PIN(004),  GPIO_INPUT)
GPIO(I2C00_SDA, PIN(003),  GPIO_INPUT)
GPIO(I2C02_SCL, PIN(0155), GPIO_INPUT)
GPIO(I2C02_SDA, PIN(0154), GPIO_INPUT)
GPIO(I2C03_SCL, PIN(010),  GPIO_INPUT)
GPIO(I2C03_SDA, PIN(007),  GPIO_INPUT)
GPIO(I2C04_SCL, PIN(0144), GPIO_INPUT)
GPIO(I2C04_SDA, PIN(0143), GPIO_INPUT)
GPIO(I2C05_SCL, PIN(0142), GPIO_INPUT)
GPIO(I2C05_SDA,	PIN(0141), GPIO_INPUT)

/* usb related */
GPIO(EN_PP3300_A,        PIN(0153), GPIO_OUT_LOW)
GPIO(EN_PP5000,          PIN(042), GPIO_OUT_LOW)
GPIO(USB_C0_SRC_EN,      PIN(0206), GPIO_OUT_LOW)
GPIO(USB_C0_SNK_EN_L,    PIN(0202), GPIO_ODR_LOW | GPIO_PULL_DOWN)
GPIO(USB_C0_SRC_HI_ILIM, PIN(0205), GPIO_OUT_LOW)
GPIO(USB_C1_SRC_EN,      PIN(0226), GPIO_OUT_LOW)
GPIO(USB_C1_SNK_EN_L,    PIN(0140), GPIO_ODR_LOW | GPIO_PULL_DOWN)
GPIO(USB_C1_SRC_HI_ILIM, PIN(0132), GPIO_OUT_LOW)


/* Fan PWM output and TACH input. PROCHOT input */
GPIO(EC_FAN1_TTACH,         PIN(050), GPIO_INPUT | GPIO_PULL_UP)
/* Fan PWM output - NC / testing only */
GPIO(EC_FAN1_PWM,           PIN(053), GPIO_OUT_LOW)

/* prochot input from devices */
GPIO(PLATFORM_EC_PROCHOT,   PIN(051), GPIO_INPUT | GPIO_PULL_UP)

/* Miscellaneous */
GPIO(PWM_KBLIGHT,           PIN(02),   GPIO_OUT_LOW)
GPIO(ENTERING_RW,           PIN(0221),  GPIO_OUT_LOW)
GPIO(ENABLE_TOUCHPAD,       PIN(0115), GPIO_OUT_LOW)
GPIO(EC_BATT_PRES_L,        PIN(0200), GPIO_INPUT)
GPIO(WLAN_OFF_L,            PIN(0245), GPIO_OUT_LOW)

/* From lid sensor */
GPIO(ENABLE_BACKLIGHT,      PIN(024), GPIO_OUT_LOW)

/* For LEDs */
GPIO(BAT_LED_GREEN_L,	PIN(0156), GPIO_OUT_HIGH)
GPIO(AC_LED_GREEN_L,	PIN(0157), GPIO_OUT_HIGH)

/* Used in baseboard/intelrvp */
GPIO(EC_SPI_OE_N, PIN(054), GPIO_OUT_LOW)

#ifdef CONFIG_KEYBOARD_COL2_INVERTED
/* KSO 1-3 Bank 1, Func1, bits 5-7 */
GPIO(KBD_KSO2, PIN(046), GPIO_KB_OUTPUT_COL2)
#endif

/*---- Alternate functions GPIO definitions ----*/

#if defined(CONFIG_CLOCK_SRC_EXTERNAL) && !defined(CONFIG_CLOCK_CRYSTAL)
/* 32KHZ_IN = GPIO_0165(Bank=3, bit=21) Func1 */
ALTERNATE(PIN_MASK(3, 0x200000), 1, MODULE_PMU, 0)
#endif

/* KB pins 8 x 13 */
/*
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

/* I2C pins alternate functions for ports 0, 2-5
 * Configure I2C00-05 as I2C alternate function.
 * If board does not use external pull-ups then change GPIO flags
 * to enable internal pull-ups.
 * I2C00_SDA = GPIO_0003(Bank=0, bit=3) Func1
 * I2C00_SCL = GPIO_0004(Bank=0, bit=4) Func1
 * I2C02_SDA = GPIO_0154(Bank=3, bit=12) Func1
 * I2C02_SCL = GPIO_0155(Bank=3, bit=13) Func1
 * I2C03_SDA = GPIO_0007(Bank=0, bit=7) Func1
 * I2C03_SDL = GPIO_0010(Bank=0, bit=8) Func1
 * I2C04_SDA = GPIO_0143(Bank=3, bit=3) Func1
 * I2C04_SCL = GPIO_0144(Bank=3, bit=4) Func1
 * I2C05_SDA = GPIO_0141(Bank=3, bit=1) Func1
 * I2C05_SDL = GPIO_0142(Bank=3, bit=2) Func1
 */
ALTERNATE(PIN_MASK(0,  0x00000198), 1, MODULE_I2C, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(3,  0x0000301E), 1, MODULE_I2C, GPIO_ODR_HIGH)

/* ADC pins
 * ADC01 = GPIO_0201(Bank=4, bit=1) Func1
 * ADC03 = GPIO_0203(Bank=4, bit=3) Func1
 * ADC04 = GPIO_0204(Bank=4, bit=4) Func1
 * ADC07 = GPIO_0207(Bank=4, bit=7) Func1
 */
ALTERNATE(PIN_MASK(4,  0x009a), 1, MODULE_ADC, GPIO_ANALOG)

