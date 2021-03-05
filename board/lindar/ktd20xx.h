/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Public header for Kinetic 36-Channel RGB LED Drivers with I2C control,
 * including KTD2061/58/59/60.
 */

#ifndef __CROS_EC_DRIVER_RGB_LED_DRIVER_KTD20XX_PUBLIC_H
#define __CROS_EC_DRIVER_RGB_LED_DRIVER_KTD20XX_PUBLIC_H

/*
 * KTD20xx Register Definition
 *
 * Reg0x00: ID Data Register
 *   skip...
 * Reg0x01: MONITOR Status Register
 *   skip...
 * Reg0x02: CONTROL Configuration Register
 *   BIT7:6 is EN_MODE[1:0]
 *     00 = global off, 01 = Night mode,
 *     10 = Normal mode, 11 = reset as default
 *   BIT5 is BrightExtendTM Enable
 *     0 = disable/1 = enable
 *   BIT4:3 is CoolExtendTM Temperature Setting
 *     00 = 135°C rising, 01 = 120°C
 *     10 = 105°C, 11 = 90°C
 *   BIT2:0 is Fade Rate Exponential Time-Constant Setting
 *     000 = 31ms, 001 = 63ms, 010 = 125ms, 011 = 250ms
 *     100 = 500ms, 101 = 1s, 110 = 2s, 111 = 4s
 *
 * Reg0x03: IRED0 Color Configuration Register
 *   IRED_SET0[7:0] Red Current Setting 0
 *     0000 0000 = 0μA
 *     0000 0001 = 125μA
 *     ...
 *     0010 1000 = 5mA
 *     ...
 *     1100 0000 = 24mA
 *     1100 0001 = 24mA (reads back as 1100 0000)
 *     ...
 *     1111 1111 = 24mA (reads back as 1100 0000)
 * Reg0x04: IGRN0 Color Configuration Register
 *   IGRN_SET0[7:0] Green Current Setting 0
 * Reg0x05: IBLU0 Color Configuration Register
 *   IBLU_SET0[7:0] Blue Current Setting 0
 * Reg0x06: IRED1 Color Configuration Register
 *   IRED_SET1[7:0] Red Current Setting 1
 * Reg0x07: IGRN1 Color Configuration Register
 *   IGRN_SET1[7:0] Green Current Setting 1
 * Reg0x08: IBLU1 Color Configuration Register
 *   IBLU_SET1[7:0] Blue Current Setting 1
 *
 * Reg0x09: ISELA12 Selection Configuration Register
 *   BIT7 is ENA1, Enable RGB with anode connected to LEDA1 pin
 *     0 = use 0μA for these LEDs (includes fade to 0μA)
 *     1 = use the settings selected by RGBA1_SEL[2:0]
 *   BIT6:4 is RGBA1_SEL[2:0]
 *     Current Selection for RGB with anode connected to LEDA1 pin
 *     0XX = I LEDA3 selects IRED_SET0[7:0]
 *     1XX = I LEDA3 selects IRED_SET1[7:0]
 *     X0X = I LEDA2 selects IGRN_SET0[7:0]
 *     X1X = I LEDA2 selects IGRN_SET1[7:0]
 *     XX0 = I LEDA4 selects IBLU_SET0[7:0]
 *     XX1 = I LEDA4 selects IBLU_SET1[7:0]
 *   BIT3 IS ENA2
 *     0 = use 0μA for these LEDs (includes fade to 0μA)
 *     1 = use the settings selected by RGBA2_SEL[2:0]
 *   BIT2:0 is RGBA2_SEL[2:0]
 *     Current Selection for RGB with anode connected to LEDA2 pin
 *     0XX = I LEDA4 selects IRED_SET0[7:0]
 *     1XX = I LEDA4 selects IRED_SET1[7:0]
 *     X0X = I LEDA3 selects IGRN_SET0[7:0]
 *     X1X = I LEDA3 selects IGRN_SET1[7:0]
 *     XX0 = I LEDA1 selects IBLU_SET0[7:0]
 *     XX1 = I LEDA1 selects IBLU_SET1[7:0]
 * Reg0x0A: ISELA34 Selection Configuration Register
 *   BIT7 is ENA3, Enable RGB with anode connected to LEDA3 pin
 *     0 = use 0μA for these LEDs (includes fade to 0μA)
 *     1 = use the settings selected by RGBA3_SEL[2:0]
 *   BIT6:4 is RGBA3_SEL[2:0]
 *     Current Selection for RGB with anode connected to LEDA3 pin
 *     0XX = I LEDA1 selects IRED_SET0[7:0]
 *     1XX = I LEDA1 selects IRED_SET1[7:0]
 *     X0X = I LEDA4 selects IGRN_SET0[7:0]
 *     X1X = I LEDA4 selects IGRN_SET1[7:0]
 *     XX0 = I LEDA2 selects IBLU_SET0[7:0]
 *     XX1 = I LEDA2 selects IBLU_SET1[7:0]
 *   BIT3 IS ENA4
 *     0 = use 0μA for these LEDs (includes fade to 0μA)
 *     1 = use the settings selected by RGBA4_SEL[2:0]
 *   BIT2:0 is RGBA4_SEL[2:0]
 *     Current Selection for RGB with anode connected to LEDA4 pin
 *     0XX = I LEDA2 selects IRED_SET0[7:0]
 *     1XX = I LEDA2 selects IRED_SET1[7:0]
 *     X0X = I LEDA1 selects IGRN_SET0[7:0]
 *     X1X = I LEDA1 selects IGRN_SET1[7:0]
 *     XX0 = I LEDA3 selects IBLU_SET0[7:0]
 *     XX1 = I LEDA3 selects IBLU_SET1[7:0]
 * Reg0x0B: ISELB12 Selection Configuration Register
 *     BIT7 is ENB1, Enable RGB with anode connected to LEDB1 pin
 *     0 = use 0μA for these LEDs (includes fade to 0μA)
 *     1 = use the settings selected by RGB1_SEL[2:0]
 *   BIT6:4 is RGBB1_SEL[2:0]
 *     Current Selection for RGB with anode connected to LEDB1 pin
 *     0XX = I LEDB3 selects IRED_SET0[7:0]
 *     1XX = I LEDB3 selects IRED_SET1[7:0]
 *     X0X = I LEDB2 selects IGRN_SET0[7:0]
 *     X1X = I LEDB2 selects IGRN_SET1[7:0]
 *     XX0 = I LEDB4 selects IBLU_SET0[7:0]
 *     XX1 = I LEDB4 selects IBLU_SET1[7:0]
 *   BIT3 IS ENB2
 *     ...
 * Reg0x0C: ISELB34 Selection Configuration Register
 *   ...
 * Reg0x0D: ISELC12 Selection Configuration Register
 *   ...
 * Reg0x0E: ISELC34 Selection Configuration Register
 *   ...
 */

enum ktd20xx_register {
	KTD20XX_ID_DATA     = 0x00,
	KTD20XX_STATUS_REG  = 0x01,
	KTD20XX_CTRL_CFG    = 0x02,
	KTD20XX_IRED_SET0   = 0x03,
	KTD20XX_IGRN_SET0   = 0x04,
	KTD20XX_IBLU_SET0   = 0x05,
	KTD20XX_IRED_SET1   = 0x06,
	KTD20XX_IGRN_SET1   = 0x07,
	KTD20XX_IBLU_SET1   = 0x08,
	KTD20XX_ISEL_A12    = 0x09,
	KTD20XX_ISEL_A34    = 0x0A,
	KTD20XX_ISEL_B12    = 0x0B,
	KTD20XX_ISEL_B34    = 0x0C,
	KTD20XX_ISEL_C12    = 0x0D,
	KTD20XX_ISEL_C34    = 0x0E,
	KTD20XX_TOTOAL_REG
};

#endif /* __CROS_EC_DRIVER_RGB_LED_DRIVER_KTD20XX_PUBLIC_H */
