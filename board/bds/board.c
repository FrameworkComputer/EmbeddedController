/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stellaris EKB-LM4F-EAC pins multiplexing */

#include "board.h"
#include "registers.h"
#include "util.h"

void configure_board(void)
{
	/* Enable all of the GPIO modules : GPIOA to GPIOQ */
	LM4_SYSTEM_RCGCGPIO = 0x7fff;

	/* GPIOA muxing :
	 * pins 0/1 : UART0 = function 1
	 * pins 2/3/4/5 : SSI0 = function 2
	 * pin 6 : GPIO = function 0 (SD card CS -- open drain)
	 * pin 7 : GPIO = function 0 (user LED)
	 */
	LM4_GPIO_PCTL(LM4_GPIO_A)  = 0x00222211;
	LM4_GPIO_AFSEL(LM4_GPIO_A) = 0x3f;
	LM4_GPIO_DEN(LM4_GPIO_A)   = 0xff;
	LM4_GPIO_PDR(LM4_GPIO_A)   = 0x00;
	LM4_GPIO_PUR(LM4_GPIO_A)   = 0x3c;
	LM4_GPIO_DIR(LM4_GPIO_A)   = 0xc0;
	LM4_GPIO_ODR(LM4_GPIO_A)   = 0x40;
	LM4_GPIO_DR2R(LM4_GPIO_A)  = 0xc3;
	LM4_GPIO_DR8R(LM4_GPIO_A)  = 0x3c;
	LM4_GPIO_DATA(LM4_GPIO_A, 0x40) = 0x40;
	LM4_GPIO_DATA(LM4_GPIO_A, 0x80) = 0;
}
