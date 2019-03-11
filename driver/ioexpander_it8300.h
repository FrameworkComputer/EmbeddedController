/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE IT8300 I/O Port expander driver header
 */

#ifndef __CROS_EC_IOEXPANDER_IT8300_H
#define __CROS_EC_IOEXPANDER_IT8300_H

#include "i2c.h"

/* Gather Interrupt Status Register */
#define IT8300_GISR		0x0

/* Interrupt Status Registers */
#define IT8300_ISR_A		0x6
#define IT8300_ISR_B		0x7
#define IT8300_ISR_C		0x28
#define IT8300_ISR_D		0x2E
#define IT8300_ISR_E		0x2F

/* Port Data Register Groups */
#define IT8300_PDGR_A		0x1
#define IT8300_PDGR_B		0x2
#define IT8300_PDGR_C		0x3
#define IT8300_PDGR_D		0x4
#define IT8300_PDGR_E		0x5

/* GPIO Port Control n Registers */
#define IT8300_GPCR_A0		0x10
#define IT8300_GPCR_A1		0x11
#define IT8300_GPCR_A2		0x12
#define IT8300_GPCR_A3		0x13
#define IT8300_GPCR_A4		0x14
#define IT8300_GPCR_A5		0x15
#define IT8300_GPCR_A6		0x16
#define IT8300_GPCR_A7		0x17

#define IT8300_GPCR_B0		0x18
#define IT8300_GPCR_B1		0x19
#define IT8300_GPCR_B2		0x1A
#define IT8300_GPCR_B3		0x1B
#define IT8300_GPCR_B4		0x1C
#define IT8300_GPCR_B5		0x1D
#define IT8300_GPCR_B6		0x1E

#define IT8300_GPCR_C0		0x20
#define IT8300_GPCR_C1		0x21
#define IT8300_GPCR_C2		0x22
#define IT8300_GPCR_C3		0x23
#define IT8300_GPCR_C4		0x24
#define IT8300_GPCR_C5		0x25
#define IT8300_GPCR_C6		0x26

#define IT8300_GPCR_D0		0x08
#define IT8300_GPCR_D1		0x09
#define IT8300_GPCR_D2		0x0A
#define IT8300_GPCR_D3		0x0B
#define IT8300_GPCR_D4		0x0C
#define IT8300_GPCR_D5		0x0D

#define IT8300_GPCR_E0		0x32

#define IT8300_GPCR_E2		0x34
#define IT8300_GPCR_E3		0x35
#define IT8300_GPCR_E4		0x36
#define IT8300_GPCR_E5		0x37
#define IT8300_GPCR_E6		0x38

#define IT8300_GPCR_GPI_MODE	BIT(7)
#define IT8300_GPCR_GP0_MODE	BIT(6)
#define IT8300_GPCR_PULL_UP_EN	BIT(2)
#define IT8300_GPCR_PULL_DN_EN	BIT(1)

/* EXGPIO Clear Alert */
#define IT8300_ECA		0x30

/* EXGPIO Alert Enable */
#define IT8300_EAE		0x31

/* Port Data Mirror Registers */
#define IT8300_PDMRA_A		0x29
#define IT8300_PDMRA_B		0x2A
#define IT8300_PDMRA_C		0x2B
#define IT8300_PDMRA_D		0x2C
#define IT8300_PDMRA_E		0x2D

/* Output Open-Drain Enable Registers */
#define IT8300_OODER_A		0x39
#define IT8300_OODER_B		0x3A
#define IT8300_OODER_C		0x3B
#define IT8300_OODER_D		0x3C
#define IT8300_OODER_E		0x3D

/* IT83200 Port GPIOs */
#define IT8300_GPX_0		BIT(0)
#define IT8300_GPX_1		BIT(1)
#define IT8300_GPX_2		BIT(2)
#define IT8300_GPX_3		BIT(3)
#define IT8300_GPX_4		BIT(4)
#define IT8300_GPX_5		BIT(5)
#define IT8300_GPX_6		BIT(6)
#define IT8300_GPX_7		BIT(7)

#endif /* __CROS_EC_IOEXPANDER_IT8300_H */
