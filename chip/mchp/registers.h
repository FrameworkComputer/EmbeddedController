/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for Microchip MEC family processors
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"

/*
 * Helper function for RAM address aliasing
 * NOTE: MCHP AHB masters do NOT require aliasing.
 * Cortex-M4 bit-banding does require aliasing of the
 * DATA SRAM region.
 */
#define MCHP_RAM_ALIAS(x)   \
	((x) >= 0x118000 ? (x) - 0x118000 + 0x20000000 : (x))

/* EC Chip Configuration */
#define MCHP_CHIP_BASE      0x400fff00
#define MCHP_CHIP_DEV_ID    REG8(MCHP_CHIP_BASE + 0x20)
#define MCHP_CHIP_DEV_REV   REG8(MCHP_CHIP_BASE + 0x21)


/* Power/Clocks/Resets */
#define MCHP_PCR_BASE       0x40080100

#define MCHP_PCR_SYS_SLP_CTL	REG32(MCHP_PCR_BASE + 0x00)
#define MCHP_PCR_PROC_CLK_CTL	REG32(MCHP_PCR_BASE + 0x04)
#define MCHP_PCR_SLOW_CLK_CTL	REG32(MCHP_PCR_BASE + 0x08)
#define MCHP_PCR_CHIP_OSC_ID	REG32(MCHP_PCR_BASE + 0x0C)
#define MCHP_PCR_PWR_RST_STS	REG32(MCHP_PCR_BASE + 0x10)
#define MCHP_PCR_PWR_RST_CTL	REG32(MCHP_PCR_BASE + 0x14)
#define MCHP_PCR_SYS_RST	REG32(MCHP_PCR_BASE + 0x18)
#define MCHP_PCR_SLP_EN0	REG32(MCHP_PCR_BASE + 0x30)
#define MCHP_PCR_SLP_EN1	REG32(MCHP_PCR_BASE + 0x34)
#define MCHP_PCR_SLP_EN2	REG32(MCHP_PCR_BASE + 0x38)
#define MCHP_PCR_SLP_EN3	REG32(MCHP_PCR_BASE + 0x3C)
#define MCHP_PCR_SLP_EN4	REG32(MCHP_PCR_BASE + 0x40)
#define MCHP_PCR_CLK_REQ0	REG32(MCHP_PCR_BASE + 0x50)
#define MCHP_PCR_CLK_REQ1	REG32(MCHP_PCR_BASE + 0x54)
#define MCHP_PCR_CLK_REQ2	REG32(MCHP_PCR_BASE + 0x58)
#define MCHP_PCR_CLK_REQ3	REG32(MCHP_PCR_BASE + 0x5C)
#define MCHP_PCR_CLK_REQ4	REG32(MCHP_PCR_BASE + 0x60)
#define MCHP_PCR_RST_EN0	REG32(MCHP_PCR_BASE + 0x70)
#define MCHP_PCR_RST_EN1	REG32(MCHP_PCR_BASE + 0x74)
#define MCHP_PCR_RST_EN2	REG32(MCHP_PCR_BASE + 0x78)
#define MCHP_PCR_RST_EN3	REG32(MCHP_PCR_BASE + 0x7C)
#define MCHP_PCR_RST_EN4	REG32(MCHP_PCR_BASE + 0x80)

#define MCHP_PCR_SLP_EN(x)	REG32(MCHP_PCR_BASE + 0x30 + ((x)<<2))
#define MCHP_PCR_CLK_REQ(x)	REG32(MCHP_PCR_BASE + 0x50 + ((x)<<2))
#define MCHP_PCR_RST_EN(x)	REG32(MCHP_PCR_BASE + 0x70 + ((x)<<2))

#define MCHP_PCR_SLP_RST_REG_MAX	(5)

/* Bit definitions for MCHP_PCR_SYS_SLP_CTL */
#define MCHP_PCR_SYS_SLP_LIGHT	(0ul << 0)
#define MCHP_PCR_SYS_SLP_HEAVY	(1ul << 0)
#define MCHP_PCR_SYS_SLP_ALL	(1ul << 3)

/*
 * Set/clear PCR sleep enable bit for single device
 * d bits[10:8] = register 0 - 4
 * d bits[4:0] = register bit position
 */
#define MCHP_PCR_SLP_EN_DEV(d)	(MCHP_PCR_SLP_EN(((d) >> 8) & 0x07) |=\
						(1ul << ((d) & 0x1f)))
#define MCHP_PCR_SLP_DIS_DEV(d)	(MCHP_PCR_SLP_EN(((d) >> 8) & 0x07) &=\
						~(1ul << ((d) & 0x1f)))

/*
 * Set/clear bit pattern specified by mask in a single PCR sleep enable
 * register.
 * id = zero based ID of sleep enable register (0-4)
 * m = bit mask of bits to change
 */
#define MCHP_PCR_SLP_EN_DEV_MASK(id, m)  MCHP_PCR_SLP_EN((id)) |= (m)
#define MCHP_PCR_SLP_DIS_DEV_MASK(id, m) MCHP_PCR_SLP_EN((id)) &= ~(m)

/* Slow Clock Control Mask */
#define MCHP_PCR_SLOW_CLK_CTL_MASK	0x03FFul

/* Sleep Enable, Clock Required, Reset on Sleep 0 bits */
#define MCHP_PCR_ISPI		(0x0002)
#define MCHP_PCR_EFUSE		(0x0001)
#define MCHP_PCR_JTAG		(0x0000)

/* Command all blocks to sleep */
#define MCHP_PCR_SLP_EN0_ISPI	BIT(2)
#define MCHP_PCR_SLP_EN0_EFUSE	BIT(1)
#define MCHP_PCR_SLP_EN0_JTAG	BIT(0)
#define MCHP_PCR_SLP_EN0_SLEEP	0x07ul

/* Sleep Enable, Clock Required, Reset on Sleep 1 bits */
#define MCHP_PCR_BTMR16_1	(BIT(8) + 31)
#define MCHP_PCR_BTMR16_0	(BIT(8) + 30)
#define MCHP_PCR_ECS		(BIT(8) + 29)
#define MCHP_PCR_PWM8		(BIT(8) + 27)
#define MCHP_PCR_PWM7		(BIT(8) + 26)
#define MCHP_PCR_PWM6		(BIT(8) + 25)
#define MCHP_PCR_PWM5		(BIT(8) + 24)
#define MCHP_PCR_PWM4		(BIT(8) + 23)
#define MCHP_PCR_PWM3		(BIT(8) + 22)
#define MCHP_PCR_PWM2		(BIT(8) + 21)
#define MCHP_PCR_PWM1		(BIT(8) + 20)
#define MCHP_PCR_TACH2		(BIT(8) + 12)
#define MCHP_PCR_TACH1		(BIT(8) + 11)
#define MCHP_PCR_I2C0		(BIT(8) + 10)
#define MCHP_PCR_WDT		(BIT(8) + 9)
#define MCHP_PCR_CPU		(BIT(8) + 8)
#define MCHP_PCR_TFDP		(BIT(8) + 7)
#define MCHP_PCR_DMA		(BIT(8) + 6)
#define MCHP_PCR_PMC		(BIT(8) + 5)
#define MCHP_PCR_PWM0		(BIT(8) + 4)
#define MCHP_PCR_TACH0		(BIT(8) + 2)
#define MCHP_PCR_PECI		(BIT(8) + 1)
#define MCHP_PCR_ECIA		(BIT(8) + 0)

/* Command all blocks to sleep */
#define MCHP_PCR_SLP_EN1_BTMR16_1	BIT(31)
#define MCHP_PCR_SLP_EN1_BTMR16_0	BIT(30)
#define MCHP_PCR_SLP_EN1_ECS		BIT(29)
/* bit[28] reserved */
#define MCHP_PCR_SLP_EN1_PWM_ALL	(BIT(4) + (0xff << 20))
#define MCHP_PCR_SLP_EN1_PWM8		BIT(27)
#define MCHP_PCR_SLP_EN1_PWM7		BIT(26)
#define MCHP_PCR_SLP_EN1_PWM6		BIT(25)
#define MCHP_PCR_SLP_EN1_PWM5		BIT(24)
#define MCHP_PCR_SLP_EN1_PWM4		BIT(23)
#define MCHP_PCR_SLP_EN1_PWM3		BIT(22)
#define MCHP_PCR_SLP_EN1_PWM2		BIT(21)
#define MCHP_PCR_SLP_EN1_PWM1		BIT(20)
/* bits[19:13] reserved */
#define MCHP_PCR_SLP_EN1_TACH2		BIT(12)
#define MCHP_PCR_SLP_EN1_TACH1		BIT(11)
#define MCHP_PCR_SLP_EN1_I2C0		BIT(10)
#define MCHP_PCR_SLP_EN1_WDT		BIT(9)
#define MCHP_PCR_SLP_EN1_CPU		BIT(8)
#define MCHP_PCR_SLP_EN1_TFDP		BIT(7)
#define MCHP_PCR_SLP_EN1_DMA		BIT(6)
#define MCHP_PCR_SLP_EN1_PMC		BIT(5)
#define MCHP_PCR_SLP_EN1_PWM0		BIT(4)
/* bit[3] reserved */
#define MCHP_PCR_SLP_EN1_TACH0		BIT(2)
#define MCHP_PCR_SLP_EN1_PECI		BIT(1)
#define MCHP_PCR_SLP_EN1_ECIA		BIT(0)
/* all sleep enable 1 bits */
#define MCHP_PCR_SLP_EN1_SLEEP		0xffffffff
/*
 * block not used by default
 * Always use ECIA, PMC, CPU and ECS
 */
#define MCHP_PCR_SLP_EN1_UNUSED_BLOCKS	0xdffffede

/* Sleep Enable2, Clock Required2, Reset on Sleep2 bits */
#define MCHP_PCR_P80CAP1		((2 << 8) + 26)
#define MCHP_PCR_P80CAP0		((2 << 8) + 25)
#define MCHP_PCR_ACPI_EC4		((2 << 8) + 23)
#define MCHP_PCR_ACPI_EC3		((2 << 8) + 22)
#define MCHP_PCR_ACPI_EC2		((2 << 8) + 21)
#define MCHP_PCR_ESPI			((2 << 8) + 19)
#define MCHP_PCR_RTC			((2 << 8) + 18)
#define MCHP_PCR_MBOX			((2 << 8) + 17)
#define MCHP_PCR_8042			((2 << 8) + 16)
#define MCHP_PCR_ACPI_PM1		((2 << 8) + 15)
#define MCHP_PCR_ACPI_EC1		((2 << 8) + 14)
#define MCHP_PCR_ACPI_EC0		((2 << 8) + 13)
#define MCHP_PCR_GCFG			((2 << 8) + 12)
#define MCHP_PCR_UART1			((2 << 8) + 2)
#define MCHP_PCR_UART0			((2 << 8) + 1)
#define MCHP_PCR_LPC			((2 << 8) + 0)

/* Command all blocks to sleep */
/* bits[31:27] reserved */
#define MCHP_PCR_SLP_EN2_P80CAP1	BIT(26)
#define MCHP_PCR_SLP_EN2_P80CAP0	BIT(25)
/* bit[24] reserved */
#define MCHP_PCR_SLP_EN2_ACPI_EC4	BIT(23)
#define MCHP_PCR_SLP_EN2_ACPI_EC3	BIT(22)
#define MCHP_PCR_SLP_EN2_ACPI_EC2	BIT(21)
/* bit[20] reserved */
#define MCHP_PCR_SLP_EN2_ESPI		BIT(19)
#define MCHP_PCR_SLP_EN2_RTC		BIT(18)
#define MCHP_PCR_SLP_EN2_MAILBOX	BIT(17)
#define MCHP_PCR_SLP_EN2_MIF8042	BIT(16)
#define MCHP_PCR_SLP_EN2_ACPI_PM1	BIT(15)
#define MCHP_PCR_SLP_EN2_ACPI_EC1	BIT(14)
#define MCHP_PCR_SLP_EN2_ACPI_EC0	BIT(13)
#define MCHP_PCR_SLP_EN2_GCFG		BIT(12)
/* bits[11:3] reserved */
#define MCHP_PCR_SLP_EN2_UART1		BIT(2)
#define MCHP_PCR_SLP_EN2_UART0		BIT(1)
#define MCHP_PCR_SLP_EN2_LPC		BIT(0)
/* all sleep enable 2 bits */
#define MCHP_PCR_SLP_EN2_SLEEP		0x07ffffff

/* Sleep Enable3, Clock Required3, Reset on Sleep3 bits */
#if defined(CHIP_FAMILY_MEC17XX)
#define MCHP_PCR_PWM9		((3 << 8) + 31)
#endif
#define MCHP_PCR_CCT0		((3 << 8) + 30)
#define MCHP_PCR_HTMR1		((3 << 8) + 29)
#define MCHP_PCR_AESHASH	((3 << 8) + 28)
#define MCHP_PCR_RNG		((3 << 8) + 27)
#define MCHP_PCR_PKE		((3 << 8) + 26)
#define MCHP_PCR_LED3		((3 << 8) + 25)
#define MCHP_PCR_BTMR32_1	((3 << 8) + 24)
#define MCHP_PCR_BTMR32_0	((3 << 8) + 23)
#define MCHP_PCR_BTMR16_3	((3 << 8) + 22)
#define MCHP_PCR_BTMR16_2	((3 << 8) + 21)
#if defined(CHIP_FAMILY_MEC17XX)
#define MCHP_PCR_GPSPI1		((3 << 8) + 20)
#elif defined(CHIP_FAMILY_MEC152X)
#define MCHP_PCR_I2C4		((3 << 8) + 20)
#endif
#define MCHP_PCR_BCM0		((3 << 8) + 19)
#define MCHP_PCR_LED2		((3 << 8) + 18)
#define MCHP_PCR_LED1		((3 << 8) + 17)
#define MCHP_PCR_LED0		((3 << 8) + 16)
#define MCHP_PCR_I2C3		((3 << 8) + 15)
#define MCHP_PCR_I2C2		((3 << 8) + 14)
#define MCHP_PCR_I2C1		((3 << 8) + 13)
#define MCHP_PCR_RPMPWM0	((3 << 8) + 12)
#define MCHP_PCR_KEYSCAN	((3 << 8) + 11)
#define MCHP_PCR_HTMR0		((3 << 8) + 10)
#define MCHP_PCR_GPSPI0		((3 << 8) + 9)
#define MCHP_PCR_PS2_2		((3 << 8) + 7)
#define MCHP_PCR_PS2_1		((3 << 8) + 6)
#define MCHP_PCR_PS2_0		((3 << 8) + 5)
#define MCHP_PCR_ADC		((3 << 8) + 3)
#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_PCR_HDMI_CEC	((3 << 8) + 1)
#endif
/* Command all blocks to sleep */
#if defined(CHIP_FAMILY_MEC17xx)
#define MCHP_PCR_SLP_EN3_PWM9		BIT(31)
#endif
#define MCHP_PCR_SLP_EN3_CCT0		BIT(30)
#define MCHP_PCR_SLP_EN3_HTMR1		BIT(29)
#define MCHP_PCR_SLP_EN3_AESHASH	BIT(28)
#define MCHP_PCR_SLP_EN3_RNG		BIT(27)
#define MCHP_PCR_SLP_EN3_PKE		BIT(26)
#define MCHP_PCR_SLP_EN3_LED3		BIT(25)
#define MCHP_PCR_SLP_EN3_BTMR32_1	BIT(24)
#define MCHP_PCR_SLP_EN3_BTMR32_0	BIT(23)
#define MCHP_PCR_SLP_EN3_BTMR16_3	BIT(22)
#define MCHP_PCR_SLP_EN3_BTMR16_2	BIT(21)
#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_PCR_SLP_EN3_I2C4		BIT(20)
#elif defined(CHIP_FAMILY_MEC17xx)
#define MCHP_PCR_SLP_EN3_GPSPI1		BIT(20)
#endif
#define MCHP_PCR_SLP_EN3_BCM0		BIT(19)
#define MCHP_PCR_SLP_EN3_LED2		BIT(18)
#define MCHP_PCR_SLP_EN3_LED1		BIT(17)
#define MCHP_PCR_SLP_EN3_LED0		BIT(16)
#define MCHP_PCR_SLP_EN3_I2C3		BIT(15)
#define MCHP_PCR_SLP_EN3_I2C2		BIT(14)
#define MCHP_PCR_SLP_EN3_I2C1		BIT(13)
#define MCHP_PCR_SLP_EN3_RPMPWM0	BIT(12)
#define MCHP_PCR_SLP_EN3_KEYSCAN	BIT(11)
#define MCHP_PCR_SLP_EN3_HTMR0		BIT(10)
#define MCHP_PCR_SLP_EN3_GPSPI0		BIT(9)
/* bit[8] reserved */
#define MCHP_PCR_SLP_EN3_PS2_2		BIT(7)
#define MCHP_PCR_SLP_EN3_PS2_1		BIT(6)
#define MCHP_PCR_SLP_EN3_PS2_0		BIT(5)
/* bit[4] reserved */
#define MCHP_PCR_SLP_EN3_ADC		BIT(3)
/* bits[2:0] reserved */
/* all sleep enable 3 bits */
#define MCHP_PCR_SLP_EN3_SLEEP		0xfffffeed
#define MCHP_PCR_SLP_EN3_PWM_ALL	(1ul << 31)
#define MCHP_PCR_SLP_EN3_LED_ALL	((0x07ul << 16) + (1ul << 25))


/* Sleep Enable4, Clock Required4, Reset on Sleep4 bits */
#define MCHP_PCR_FJCL		((4 << 8) + 15)
#define MCHP_PCR_PSPI		((4 << 8) + 14)
#define MCHP_PCR_PROCHOT	((4 << 8) + 13)
#define MCHP_PCR_RCID2		((4 << 8) + 12)
#define MCHP_PCR_RCID1		((4 << 8) + 11)
#define MCHP_PCR_RCID0		((4 << 8) + 10)
#define MCHP_PCR_BCM1		((4 << 8) + 9)
#define MCHP_PCR_QMSPI		((4 << 8) + 8)
#if defined(CHIP_FAMILY_MEC17XX)
#define MCHP_PCR_RPMPWM1	((4 << 8) + 7)
#define MCHP_PCR_RTMR		((4 << 8) + 6)
#define MCHP_PCR_CNT16_3	((4 << 8) + 5)
#elif defined(CHIP_FAMILY_MEC152X)
#define MCHP_PCR_I2C_S_2	((4 << 8) + 7)
#define MCHP_PCR_I2C_S_1	((4 << 8) + 6)
#define MCHP_PCR_I2C_S_0	((4 << 8) + 5)
#endif
#define MCHP_PCR_CNT16_2	((4 << 8) + 4)
#define MCHP_PCR_CNT16_1	((4 << 8) + 3)
#define MCHP_PCR_CNT16_0	((4 << 8) + 2)
#if defined(CHIP_FAMILY_MEC17XX)
#define MCHP_PCR_PWM11		((4 << 8) + 1)
#define MCHP_PCR_PWM10		((4 << 8) + 0)
#endif

/* Command all blocks to sleep */
#define MCHP_PCR_SLP_EN4_FJCL		BIT(15)
#define MCHP_PCR_SLP_EN4_PSPI		BIT(14)
#define MCHP_PCR_SLP_EN4_PROCHOT	BIT(13)
#define MCHP_PCR_SLP_EN4_RCID2		BIT(12)
#define MCHP_PCR_SLP_EN4_RCID1		BIT(11)
#define MCHP_PCR_SLP_EN4_RCID0		BIT(10)
#define MCHP_PCR_SLP_EN4_BCM1		BIT(9)
#define MCHP_PCR_SLP_EN4_QMSPI		BIT(8)
#define MCHP_PCR_SLP_EN4_RPMPWM1	BIT(7)
#define MCHP_PCR_SLP_EN4_RTMR		BIT(6)
#define MCHP_PCR_SLP_EN4_CNT16_3	BIT(5)
#define MCHP_PCR_SLP_EN4_CNT16_2	BIT(4)
#define MCHP_PCR_SLP_EN4_CNT16_1	BIT(3)
#define MCHP_PCR_SLP_EN4_CNT16_0	BIT(2)
#define MCHP_PCR_SLP_EN4_PWM_ALL	(3 << 0)
#if defined(CHIP_FAMILY_MEC17xx)
#define MCHP_PCR_SLP_EN4_PWM11		BIT(1)
#define MCHP_PCR_SLP_EN4_PWM10		BIT(0)
#endif
/* all sleep enable 4 bits */
#define MCHP_PCR_SLP_EN4_SLEEP		0x0000ffff

/* Allow all blocks to request clocks */
#define MCHP_PCR_SLP_EN0_WAKE	(~(MCHP_PCR_SLP_EN0_SLEEP))
#define MCHP_PCR_SLP_EN1_WAKE	(~(MCHP_PCR_SLP_EN1_SLEEP))
#define MCHP_PCR_SLP_EN2_WAKE	(~(MCHP_PCR_SLP_EN2_SLEEP))
#define MCHP_PCR_SLP_EN3_WAKE	(~(MCHP_PCR_SLP_EN3_SLEEP))
#define MCHP_PCR_SLP_EN4_WAKE	(~(MCHP_PCR_SLP_EN4_SLEEP))


/* Bit definitions for MCHP_PCR_SLP_EN1/CLK_REQ1/RST_EN1 */

/* Bit definitions for MCHP_PCR_SLP_EN2/CLK_REQ2/RST_EN2 */

/* Bit definitions for MCHP_PCR_SLP_EN3/CLK_REQ3/RST_EN3 */
#define MCHP_PCR_SLP_EN1_PKE		BIT(26)
#define MCHP_PCR_SLP_EN1_NDRNG		BIT(27)
#define MCHP_PCR_SLP_EN1_AES_SHA	BIT(28)
#define MCHP_PCR_SLP_EN1_ALL_CRYPTO	(0x07 << 26)

/* Bit definitions for MCHP_PCR_SLP_EN4/CLK_REQ4/RST_EN4 */


/* Bit defines for MCHP_PCR_PWR_RST_STS */
#define MCHP_PWR_RST_STS_VTR		BIT(6)
#define MCHP_PWR_RST_STS_VBAT		BIT(5)

/* Bit defines for MCHP_PCR_PWR_RST_CTL */
#define MCHP_PCR_PWR_HOST_RST_SEL_BITPOS	8
#define MCHP_PCR_PWR_HOST_RST_LRESET		1
#define MCHP_PCR_PWR_HOST_RST_ESPI_PLTRST	0


/* Bit defines for MCHP_PCR_SYS_RST */
#define MCHP_PCR_SYS_SOFT_RESET  BIT(8)


/* TFDP */
#define MCHP_TFDP_BASE	0x40008c00
#define MCHP_TFDP_DATA	REG8(MCHP_TFDP_BASE + 0x00)
#define MCHP_TFDP_CTRL	REG8(MCHP_TFDP_BASE + 0x04)


/* EC Subsystem */
#define MCHP_EC_BASE			0x4000fc00
#define MCHP_EC_AHB_ERR			REG32(MCHP_EC_BASE + 0x04)
#define MCHP_EC_ID_RO			REG32(MCHP_EC_BASE + 0x10)
#define MCHP_EC_AHB_ERR_EN		REG32(MCHP_EC_BASE + 0x14)
#define MCHP_EC_INT_CTRL		REG32(MCHP_EC_BASE + 0x18)
#define MCHP_EC_TRACE_EN		REG32(MCHP_EC_BASE + 0x1c)
#define MCHP_EC_JTAG_EN			REG32(MCHP_EC_BASE + 0x20)
#define MCHP_EC_WDT_CNT			REG32(MCHP_EC_BASE + 0x28)
#define MCHP_EC_AES_SHA_SWAP_CTRL	REG8(MCHP_EC_BASE + 0x2c)
#define MCHP_EC_CRYPTO_SRESET		REG8(MCHP_EC_BASE + 0x5c)
#define MCHP_EC_GPIO_BANK_PWR		REG8(MCHP_EC_BASE + 0x64)

/* MCHP_EC_JTAG_EN bit definitions */
#define MCHP_JTAG_ENABLE		0x01
/* bits [2:1] */
#define MCHP_JTAG_MODE_4PIN		0x00
/* ARM 2-pin SWD plus 1-pin Serial Wire Viewer (ITM) */
#define MCHP_JTAG_MODE_SWD_SWV		0x02
/* ARM 2-pin SWD with no SWV */
#define MCHP_JTAG_MODE_SWD		0x04


/* MCHP_EC_CRYPTO_SRESET bit definitions */
#define MCHP_CRYPTO_NDRNG_SRST			0x01
#define MCHP_CRYPTO_PKE_SRST			0x02
#define MCHP_CRYPTO_AES_SHA_SRST		0x04
#define MCHP_CRYPTO_ALL_SRST			0x07

/* MCHP_GPIO_BANK_PWR bit definitions */
#define MCHP_EC_GPIO_BANK_PWR_VTR1_18	(0x01)
#define MCHP_EC_GPIO_BANK_PWR_VTR2_18	(0x02)
#define MCHP_EC_GPIO_BANK_PWR_VTR3_18	(0x04)


/* AHB ERR Enable */
#define MCHP_EC_AHB_ERROR_ENABLE	0
#define MCHP_EC_AHB_ERROR_DISABLE	1

#define MCHP_WEEK_TIMER_BASE        0x4000ac80
#define MCHP_WEEK_TIMER_BGPO_POWER  REG32(MCHP_WEEK_TIMER_BASE + 0x20)
#define MCHP_WEEK_TIMER_BGPO_RESET  REG32(MCHP_WEEK_TIMER_BASE + 0x24)


/* Interrupt aggregator */
#define MCHP_INT_BASE       0x4000e000
#define MCHP_INTx_BASE(x)   (MCHP_INT_BASE + ((x)<<4) + ((x)<<2) - 160)
#define MCHP_INT_SOURCE(x)  REG32(MCHP_INTx_BASE(x) + 0x0)
#define MCHP_INT_ENABLE(x)  REG32(MCHP_INTx_BASE(x) + 0x4)
#define MCHP_INT_RESULT(x)  REG32(MCHP_INTx_BASE(x) + 0x8)
#define MCHP_INT_DISABLE(x) REG32(MCHP_INTx_BASE(x) + 0xc)
#define MCHP_INT_BLK_EN     REG32(MCHP_INT_BASE + 0x200)
#define MCHP_INT_BLK_DIS    REG32(MCHP_INT_BASE + 0x204)
#define MCHP_INT_BLK_IRQ    REG32(MCHP_INT_BASE + 0x208)
#define MCHP_INT_GIRQ_FIRST	8
#define MCHP_INT_GIRQ_LAST	26
#define MCHP_INT_GIRQ_NUM	(26-8+1)

/*
 * Bits for INT=13(GIRQ13) registers
 * SMBus[0:3] = bits[0:3]
 */
#define MCHP_INT13_SMB(x)		(1ul << (x))


/*
 * Bits for INT=14(GIRQ14) registers
 * DMA channels 0 - 13
 */
#define MCHP_INT14_DMA(x)		(1ul << (x))


/* Bits for INT=15(GIRQ15) registers
 * UART[0:1] = bits[0:1]
 * EMI[0:2] = bits[2:4]
 */
#define MCHP_INT15_UART(x)		(1ul << ((x) & 0x01))
#define MCHP_INT15_EMI(x)		(1ul << (2 + (x)))
/*
 * ACPI_EC[0:4] IBF = bits[5,7,9,11,13]
 * ACPI_EC[0:4] OBE = bits[6,8,10,12,14]
 */
#define MCHP_INT15_ACPI_EC_IBF(x)	(1ul << (5 + ((x) << 1)))
#define MCHP_INT15_ACPI_EC_OBE(x)	(1ul << (6 + ((x) << 1)))
#define MCHP_INT15_ACPI_PM1_CTL		(1ul << 15)
#define MCHP_INT15_ACPI_PM1_EN		(1ul << 16)
#define MCHP_INT15_ACPI_PM1_STS		(1ul << 17)
#define MCHP_INT15_8042_OBE		(1ul << 18)
#define MCHP_INT15_8042_IBF		(1ul << 19)
#define MCHP_INT15_MAILBOX		(1ul << 20)
#define MCHP_INT15_P80(x)		(1ul << (22 + ((x) & 0x01)))


/* Bits for INT=16(GIRQ16) registers */
#define MCHP_INT16_PKE_ERR		(1ul << 0)
#define MCHP_INT16_PKE_DONE		(1ul << 1)
#define MCHP_INT16_RNG_DONE		(1ul << 2)
#define MCHP_INT16_AES_DONE		(1ul << 3)
#define MCHP_INT16_HASH_DONE		(1ul << 4)


/* Bits for INT=17(GIRQ17) registers */
#define MCHP_INT17_PECI			(1ul << 0)
/*    TACH[0:2] = bits[1:3] */
#define MCHP_INT17_TACH(x)		(1ul << (1 + (x)))
/*    RPMFAN_FAIL[0:1] = bits[4,6] */
#define MCHP_INT17_RPMFAN_FAIL(x)	(1ul << (4 + ((x) << 1)))
/*    RPMFAN_STALL[0:1] = bits[5,7] */
#define MCHP_INT17_RPMFAN_STALL(x)	(1ul << (5 + ((x) << 1)))
#define MCHP_INT17_ADC_SINGLE		(1ul << 8)
#define MCHP_INT17_ADC_REPEAT		(1ul << 9)
/*    RCIC[0:2] = bits[10:12] */
#define MCHP_INT17_RCID(x)		(1ul << (10 + (x)))
#define MCHP_INT17_LED_WDT(x)		(1ul << (13 + (x)))


/* Bits for INT=18(GIRQ18) registers */
#define MCHP_INT18_LPC			(1ul << 0)
#define MCHP_INT18_QMSPI0		(1ul << 1)
/*    SPI_TX[0:1] = bits[2,4] */
#define MCHP_INT18_SPI_TX(x)		(1ul << (2 + ((x) << 1)))
/*    SPI_RX[0:1] = bits[3,5] */
#define MCHP_INT18_SPI_RX(x)		(1ul << (3 + ((x) << 1)))


/* Bits for INT=19(GIRQ19) registers */
#define MCHP_INT19_ESPI_PC		(1ul << 0)
#define MCHP_INT19_ESPI_BM1		(1ul << 1)
#define MCHP_INT19_ESPI_BM2		(1ul << 2)
#define MCHP_INT19_ESPI_LTR		(1ul << 3)
#define MCHP_INT19_ESPI_OOB_TX		(1ul << 4)
#define MCHP_INT19_ESPI_OOB_RX		(1ul << 5)
#define MCHP_INT19_ESPI_FC		(1ul << 6)
#define MCHP_INT19_ESPI_RESET		(1ul << 7)
#define MCHP_INT19_ESPI_VW_EN		(1ul << 8)


/* Bits for INT=21(GIRQ21) registers */
#define MCHP_INT21_RTOS_TMR		(1ul << 0)
/*    HibernationTimer[0:1] = bits[1:2] */
#define MCHP_INT21_HIB_TMR(x)		(1ul << (1 + (x)))
#define MCHP_INT21_WEEK_ALARM		(1ul << 3)
#define MCHP_INT21_WEEK_SUB		(1ul << 4)
#define MCHP_INT21_WEEK_1SEC		(1ul << 5)
#define MCHP_INT21_WEEK_1SEC_SUB	(1ul << 6)
#define MCHP_INT21_WEEK_PWR_PRES	(1ul << 7)
#define MCHP_INT21_RTC			(1ul << 8)
#define MCHP_INT21_RTC_ALARM		(1ul << 9)
#define MCHP_INT21_VCI_OVRD		(1ul << 10)
/*    VCI_IN[0:6] = bits[11:17] */
#define MCHP_INT21_VCI_IN(x)		(1ul << (11 + (x)))
/*    PS2 Port Wake[0:4] =[0A,0B,1A,1B,2] = bits[18:22] */
#define MCHP_INT21_PS2_WAKE(x)		(1ul << (18 + (x)))
#define MCHP_INT21_KEYSCAN		(1ul << 25)

/* Bits for INT=22(GIRQ22) wake only registers */
#define MCHP_INT22_WAKE_ONLY_LPC	(1ul << 0)
#define MCHP_INT22_WAKE_ONLY_I2C0	(1ul << 1)
#define MCHP_INT22_WAKE_ONLY_I2C1	(1ul << 2)
#define MCHP_INT22_WAKE_ONLY_I2C2	(1ul << 3)
#define MCHP_INT22_WAKE_ONLY_I2C3	(1ul << 4)
#define MCHP_INT22_WAKE_ONLY_ESPI	(1ul << 9)

/* Bits for INT=23(GIRQ23) registers */
/*    16-bit Basic Timers[0:3] = bits[0:3] */
#define MCHP_INT23_BASIC_TMR16(x)	(1ul << (x))
/*    32-bit Basic Timers[0:1] = bits[4:5] */
#define MCHP_INT23_BASIC_TMR32(x)	(1ul << (4 + (x)))
/*    16-bit Counter-Timer[0:3] = bits[6:9] */
#define MCHP_INT23_CNT(x)		(1ul << (6 + (x)))
#define MCHP_INT23_CCT_TMR		(1ul << 10)
/*    CCT Capture events[0:5] = bits[11:16] */
#define MCHP_INT23_CCT_CAP(x)		(1ul << (11 + (x)))
/*    CCT Compare events[0:1] = bits[17:18] */
#define MCHP_INT23_CCT_CMP(x)		(1ul << (17 + (x)))


/* Bits for INT=24(GIRQ24) registers */
/*    Master-to-Slave v=[0:6], Source=[0:3] */
#define MCHP_INT24_MSVW_SRC(v, s)	(1ul << ((4 * (v)) + (s)))

/* Bits for INT25(GIRQ25) registers */
/*    Master-to-Slave v=[7:10], Source=[0:3] */
#define MCHP_INT25_MSVW_SRC(v, s)	(1ul << ((4 * ((v)-7)) + (s)))

/* End MCHP_INTxy bit definitions */

/* UART Peripheral */
#define MCHP_UART_CONFIG_BASE(x)     (0x400f2700 + ((x) * 0x400))
#define MCHP_UART_RUNTIME_BASE(x)    (0x400f2400 + ((x) * 0x400))

#define MCHP_UART_ACT(x)     REG8(MCHP_UART_CONFIG_BASE(x) + 0x30)
#define MCHP_UART_CFG(x)     REG8(MCHP_UART_CONFIG_BASE(x) + 0xf0)

/* DLAB=0 */
#define MCHP_UART_RB(x) /*R*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x0)
#define MCHP_UART_TB(x) /*W*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x0)
#define MCHP_UART_IER(x)      REG8(MCHP_UART_RUNTIME_BASE(x) + 0x1)
/* DLAB=1 */
#define MCHP_UART_PBRG0(x)    REG8(MCHP_UART_RUNTIME_BASE(x) + 0x0)
#define MCHP_UART_PBRG1(x)    REG8(MCHP_UART_RUNTIME_BASE(x) + 0x1)

#define MCHP_UART_FCR(x) /*W*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x2)
#define MCHP_UART_IIR(x) /*R*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x2)
#define MCHP_UART_LCR(x)       REG8(MCHP_UART_RUNTIME_BASE(x) + 0x3)
#define MCHP_UART_MCR(x)       REG8(MCHP_UART_RUNTIME_BASE(x) + 0x4)
#define MCHP_UART_LSR(x)       REG8(MCHP_UART_RUNTIME_BASE(x) + 0x5)
#define MCHP_UART_MSR(x)       REG8(MCHP_UART_RUNTIME_BASE(x) + 0x6)
#define MCHP_UART_SCR(x)       REG8(MCHP_UART_RUNTIME_BASE(x) + 0x7)
/*
 * UART[0:1] connected to GIRQ15 bits[0:1]
 */
#define MCHP_UART_GIRQ		15
#define MCHP_UART_GIRQ_BIT(x)	(1ul << (x))

/* Bit defines for MCHP_UARTx_LSR */
#define MCHP_LSR_TX_EMPTY     BIT(5)


/* GPIO */
#define MCHP_GPIO_BASE      0x40081000


/* MCHP each Port contains 32 GPIO's.
 * GPIO Control 1 registers are 32-bit registers starting at
 * MCHP_GPIO_BASE.
 * index = octal GPIO number from MCHP specification.
 * port/bank = index >> 5
 * id = index & 0x1F
 *
 * The port/bank, id pair may also be used to access GPIO's via
 * parallel I/O registers if GPIO control is configured for
 * parallel I/O.
 *
 * From ec/chip/mec1701/config_chip.h
 * #define GPIO_PIN(index) ((index) >> 5), ((index) & 0x1F)
 *
 * GPIO Control 1 Address = 0x40081000 + (((bank << 5) + id) << 2)
 *
 * Example: GPIO043, Control 1 register address = 0x4008108c
 * port/bank = 0x23 >> 5 = 1
 * id        = 0x23 & 0x1F = 0x03
 * Control 1 Address = 0x40081000 + ((BIT(5) + 0x03) << 2) = 0x4008108c
 *
 * Example: GPIO235, Control 1 register address = 0x40081274
 * port/bank = 0x9d >> 5   = 4
 * id        = 0x9d & 0x1f = 0x1d
 * Control 1 Address = 0x40081000 + (((4 << 5) + 0x1d) << 2) = 0x40081274
 *
 */
#define MCHP_GPIO_CTL(port, id) REG32(MCHP_GPIO_BASE + \
	(((port << 5) + id) << 2))

#define MCHP_GPIO_CTL2(port, id) REG32(MCHP_GPIO_BASE + 0x500 + \
	(((port << 5) + id) << 2))

/* MCHP implements 6 GPIO ports */
#define MCHP_GPIO_MAX_PORT	(7)

#define UNIMPLEMENTED_GPIO_BANK 0

/*
 * MEC1701 documentation GPIO numbers are octal, each control
 * register is located on a 32-bit boundary.
 */
#define MCHP_GPIO_CTRL(gpio_num) REG32(MCHP_GPIO_BASE + \
	((gpio_num) << 2))

#define MCHP_GPIO_CTRL2(gpio_num) REG32(MCHP_GPIO_BASE + 0x500 + \
	((gpio_num) << 2))

/*
 * GPIO control register bit fields
 */
#define MCHP_GPIO_CTRL_PUD_BITPOS	0
#define MCHP_GPIO_CTRL_PUD_MASK0	0x03
#define MCHP_GPIO_CTRL_PUD_MASK		0x03
#define MCHP_GPIO_CTRL_PUD_NONE		0x00
#define MCHP_GPIO_CTRL_PUD_PU		0x01
#define MCHP_GPIO_CTRL_PUD_PD		0x02
#define MCHP_GPIO_CTRL_PUD_KEEPER	0x03
#define MCHP_GPIO_CTRL_PWR_BITPOS	2
#define MCHP_GPIO_CTRL_PWR_MASK0	0x03
#define MCHP_GPIO_CTRL_PWR_MASK		(0x03 << 2)
#define MCHP_GPIO_CTRL_PWR_VTR		(0x00 << 2)
#define MCHP_GPIO_CTRL_PWR_OFF		(0x02 << 2)
#define MCHP_GPIO_INTDET_MASK		(0xF0)
#define MCHP_GPIO_INTDET_LVL_LO		(0x00)
#define MCHP_GPIO_INTDET_LVL_HI		(0x10)
#define MCHP_GPIO_INTDET_DISABLED	(0x40)
#define MCHP_GPIO_INTDET_EDGE_RIS	(0xD0)
#define MCHP_GPIO_INTDET_EDGE_FALL	(0xE0)
#define MCHP_GPIO_INTDET_EDGE_BOTH	(0xF0)
#define MCHP_GPIO_INTDET_EDGE_EN	(1u << 7)
#define MCHP_GPIO_PUSH_PULL		(0u << 8)
#define MCHP_GPIO_OPEN_DRAIN		(1u << 8)
#define MCHP_GPIO_INPUT			(0u << 9)
#define MCHP_GPIO_OUTPUT		(1u << 9)
#define MCHP_GPIO_OUTSET_CTRL		(0u << 10)
#define MCHP_GPIO_OUTSEL_PAR		(1u << 10)
#define MCHP_GPIO_POLARITY_NINV		(0u << 11)
#define MCHP_GPIO_POLARITY_INV		(1u << 11)
#define MCHP_GPIO_CTRL_ALT_FUNC_BITPOS	12
#define MCHP_GPIO_CTRL_ALT_FUNC_MASK0	0x03
#define MCHP_GPIO_CTRL_ALT_FUNC_MASK	(0x03 << 12)
#define MCHP_GPIO_CTRL_FUNC_GPIO	(0 << 12)
#define MCHP_GPIO_CTRL_FUNC_1		(1 << 12)
#define MCHP_GPIO_CTRL_FUNC_2		(2 << 12)
#define MCHP_GPIO_CTRL_FUNC_3		(3 << 12)
#define MCHP_GPIO_CTRL_INPUT_DISABLE_MASK (0x01 << 15)
#define MCHP_GPIO_CTRL_INPUT_ENABLE (0x00 << 15)

#define MCHP_GPIO_CTRL_OUT_LVL		BIT(16)
#define MCHP_GPIO_CTRL_IN_LVL		BIT(24)


#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_BITPOS		4
#define MCHP_GPIO_CTRL2_SLEW_RATE_MASK				0x01

#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_MASK0		0x03
#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_MASK			(0x03 << 4)
#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_2MA			(0x00)
#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_4MA			(0x10)
#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_8MA			(0x20)
#define MCHP_GPIO_CTRL2_DRIVE_STRENGTH_12MA			(0x30)




/* GPIO Parallel Input and Output registers.
 * gpio_bank in [0, 5]
 */
#define MCHP_GPIO_PARIN(gpio_bank) REG32(MCHP_GPIO_BASE + 0x0300 +\
	((gpio_bank) << 2))

#define MCHP_GPIO_PAROUT(gpio_bank) REG32(MCHP_GPIO_BASE + 0x0380 +\
	((gpio_bank) << 2))

/* Timer */

#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_TMR16_MAX		(2)
#else 
#define MCHP_TMR16_MAX		(4)
#endif /* CHIP_FAMILY_MEC152X */

#define MCHP_TMR32_MAX		(2)
#define MCHP_TMR16_BASE(x)  (0x40000c00 + (x) * 0x20)
#define MCHP_TMR32_BASE(x)  (0x40000c80 + (x) * 0x20)

#define MCHP_TMR16_CNT(x)   REG32(MCHP_TMR16_BASE(x) + 0x0)
#define MCHP_TMR16_PRE(x)   REG32(MCHP_TMR16_BASE(x) + 0x4)
#define MCHP_TMR16_STS(x)   REG32(MCHP_TMR16_BASE(x) + 0x8)
#define MCHP_TMR16_IEN(x)   REG32(MCHP_TMR16_BASE(x) + 0xc)
#define MCHP_TMR16_CTL(x)   REG32(MCHP_TMR16_BASE(x) + 0x10)
#define MCHP_TMR32_CNT(x)   REG32(MCHP_TMR32_BASE(x) + 0x0)
#define MCHP_TMR32_PRE(x)   REG32(MCHP_TMR32_BASE(x) + 0x4)
#define MCHP_TMR32_STS(x)   REG32(MCHP_TMR32_BASE(x) + 0x8)
#define MCHP_TMR32_IEN(x)   REG32(MCHP_TMR32_BASE(x) + 0xc)
#define MCHP_TMR32_CTL(x)   REG32(MCHP_TMR32_BASE(x) + 0x10)
/* 16-bit Basic Timers[0:3] = GIRQ23 bits[0:3] */
#define MCHP_TMR16_GIRQ		23
#define MCHP_TMR16_GIRQ_BIT(x)	(1ul << (x))
/* 32-bit Basic Timers[0:1] = GIRQ23 bits[4:5] */
#define MCHP_TMR32_GIRQ		23
#define MCHP_TMR32_GIRQ_BIT(x)	(1ul << ((x) + 4))

/* RTimer */
#define MCHP_RTMR_BASE		(0x40007400)
#define MCHP_RTMR_COUNTER	REG32(MCHP_RTMR_BASE + 0x00)
#define MCHP_RTMR_PRELOAD	REG32(MCHP_RTMR_BASE + 0x04)
#define MCHP_RTMR_CONTROL	REG8(MCHP_RTMR_BASE + 0x08)
#define MCHP_RTMR_SOFT_INTR	REG8(MCHP_RTMR_BASE + 0x0c)
/* RTimer GIRQ21 bit[0] */
#define MCHP_RTMR_GIRQ		21
#define MCHP_RTMR_GIRQ_BIT(x)	(1ul << 0)

/* Watchdog */
#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_WDG_BASE       0x40000400
#define MCHP_WDG_LOAD       REG16(MCHP_WDG_BASE + 0x0)
#define MCHP_WDG_CTL        REG32(MCHP_WDG_BASE + 0x4)
#define MCHP_WDG_KICK       REG8(MCHP_WDG_BASE + 0x8)
#define MCHP_WDG_CNT        REG16(MCHP_WDG_BASE + 0xc)
#define MCHP_WDG_STATUS     REG32(MCHP_WDG_BASE + 0x10)
#define MCHP_WDG_INT_EN     REG32(MCHP_WDG_BASE + 0x14)
#else
#define MCHP_WDG_BASE       0x40000000
#define MCHP_WDG_LOAD       REG16(MCHP_WDG_BASE + 0x0)
#define MCHP_WDG_CTL        REG8(MCHP_WDG_BASE + 0x4)
#define MCHP_WDG_KICK       REG8(MCHP_WDG_BASE + 0x8)
#define MCHP_WDG_CNT        REG16(MCHP_WDG_BASE + 0xc)
#endif 



/* VBAT */
#define MCHP_VBAT_BASE               0x4000a400
#define MCHP_VBAT_STS                REG32(MCHP_VBAT_BASE + 0x0)
#define MCHP_VBAT_CE                 REG32(MCHP_VBAT_BASE + 0x8)
#define MCHP_VBAT_SHDN_DIS           REG32(MCHP_VBAT_BASE + 0xC)
#define MCHP_VBAT_MONOTONIC_CTR_LO   REG32(MCHP_VBAT_BASE + 0x20)
#define MCHP_VBAT_MONOTONIC_CTR_HI   REG32(MCHP_VBAT_BASE + 0x24)

#define MCHP_VBAT_RAM_BASE			 0x4000a800
/* read 32-bit word at 32-bit offset x where 0 <= x <= 31 */
#define MCHP_VBAT_RAM(x)		REG32(MCHP_VBAT_RAM_BASE + ((x) * 4))
#define MCHP_VBAT_RAM8(x)		REG8(MCHP_VBAT_RAM_BASE + (x))

#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_VBAT_VWIRE_BACKUP       14
#else

#define MCHP_VBAT_VWIRE_BACKUP       30
#endif 

/* Bit definition for MCHP_VBAT_STS */
#define MCHP_VBAT_STS_SOFTRESET		BIT(2)
#define MCHP_VBAT_STS_RESETI		BIT(4)
#define MCHP_VBAT_STS_WDT	        BIT(5)
#define MCHP_VBAT_STS_SYSRESETREQ	BIT(6)
#define MCHP_VBAT_STS_VBAT_RST		BIT(7)
#define MCHP_VBAT_STS_ANY_RST		(0xF4u)

/* Bit definitions for MCHP_VBAT_CE */
#define MCHP_VBAT_CE_XOSEL_BITPOS    (3)
#define MCHP_VBAT_CE_XOSEL_MASK      (1ul << 3)
#define MCHP_VBAT_CE_XOSEL_PAR       (0ul << 3)
#define MCHP_VBAT_CE_XOSEL_SE        (1ul << 3)

#define MCHP_VBAT_CE_32K_SRC_BITPOS  (2)
#define MCHP_VBAT_CE_32K_SRC_MASK    (1ul << 2)
#define MCHP_VBAT_CE_32K_SRC_INT     (0ul << 2)
#define MCHP_VBAT_CE_32K_SRC_CRYS    (1ul << 2)

#define MCHP_VBAT_CE_EXT_32K_BITPOS  (1)
#define MCHP_VBAT_CE_EXT_32K_MASK    (1ul << 1)
#define MCHP_VBAT_CE_INT_32K         (0ul << 1)
#define MCHP_VBAT_CE_EXT_32K         (1ul << 1)

#define MCHP_VBAT_CE_32K_VTR_BITPOS  (0)
#define MCHP_VBAT_CE_32K_VTR_MASK    (1ul << 0)
#define MCHP_VBAT_CE_32K_VTR_ON      (0ul << 0)
#define MCHP_VBAT_CE_32K_VTR_OFF     (1ul << 0)


/*
 * Blinking-Breathing LED
 * 4 instances
 */
#define MCHP_BBLED_BASE(x)	(0x4000B800 + (((x) & 0x03) << 8))
#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_BBLEN_INSTANCES	3
#else
#define MCHP_BBLEN_INSTANCES	4
#endif

#define MCHP_BBLED_CONFIG(x)		REG32(MCHP_BBLED_BASE(x) + 0x00)
#define MCHP_BBLED_LIMITS(x)		REG32(MCHP_BBLED_BASE(x) + 0x04)
#define MCHP_BBLED_LIMIT_MIN(x)		REG8(MCHP_BBLED_BASE(x) + 0x04)
#define MCHP_BBLED_LIMIT_MAX(x)		REG8(MCHP_BBLED_BASE(x) + 0x06)
#define MCHP_BBLED_DELAY(x)		REG32(MCHP_BBLED_BASE(x) + 0x08)
#define MCHP_BBLED_UPDATE_STEP(x)	REG32(MCHP_BBLED_BASE(x) + 0x0C)
#define MCHP_BBLED_UPDATE_INTV(x)	REG32(MCHP_BBLED_BASE(x) + 0x10)
#define MCHP_BBLED_OUTPUT_DLY(x)	REG8(MCHP_BBLED_BASE(x) + 0x14)

/* BBLED Configuration Register */
#define MCHP_BBLED_ASYMMETRIC		(1ul << 16)
#define MCHP_BBLED_WDT_RELOAD_BITPOS	(8)
#define MCHP_BBLED_WDT_RELOAD_MASK0	(0xFFul)
#define MCHP_BBLED_WDT_RELOAD_MASK	(0xFFul << 8)
#define MCHP_BBLED_RESET		(1ul << 7)
#define MCHP_BBLED_EN_UPDATE		(1ul << 6)
#define MCHP_BBLED_PWM_SIZE_BITPOS	(4)
#define MCHP_BBLED_PWM_SIZE_MASK0	(0x03ul)
#define MCHP_BBLED_PWM_SIZE_MASK	(0x03ul << 4)
#define MCHP_BBLED_PWM_SIZE_6BIT	(0x02ul << 4)
#define MCHP_BBLED_PWM_SIZE_7BIT	(0x01ul << 4)
#define MCHP_BBLED_PWM_SIZE_8BIT	(0x00ul << 4)
#define MCHP_BBLED_SYNC			(1ul << 3)
#define MCHP_BBLED_CLK_48M		(1ul << 2)
#define MCHP_BBLED_CLK_32K		(0ul << 2)
#define MCHP_BBLED_CTRL_MASK		(0x03ul)
#define MCHP_BBLED_CTRL_ALWAYS_ON	(0x03ul)
#define MCHP_BBLED_CTRL_BLINK		(0x02ul)
#define MCHP_BBLED_CTRL_BREATHE		(0x01ul)
#define MCHP_BBLED_CTRL_OFF		(0x00ul)

/* BBLED Delay Register */
#define MCHP_BBLED_DLY_MASK		(0x0FFFul)
#define MCHP_BBLED_DLY_LO_BITPOS	(0)
#define MCHP_BBLED_DLY_LO_MASK		(0x0FFFul << 0)
#define MCHP_BBLED_DLY_HI_BITPOS	(12)
#define MCHP_BBLED_DLY_HI_MASK		(0x0FFFul << 12)

/*
 * BBLED Update Step Register
 * 8 update fields numbered 0 - 7
 */
#define MCHP_BBLED_UPD_STEP_MASK0	(0x0Ful)
#define MCHP_BBLED_UPD_STEP_MASK(u)	(0x0Ful << (((u) & 0x07) + 4))

/*
 * BBLED Update Interval Register
 * 8 interval fields numbered 0 - 7
 */
#define MCHP_BBLED_UPD_INTV_MASK0	(0x0Ful)
#define MCHP_BBLED_UPD_INTV_MASK(i)	(0x0Ful << (((i) & 0x07) + 4))


/* Miscellaneous firmware control fields
 * scratch pad index cannot be more than 32 as
 * MCHP has 128 bytes = 32 indexes of scratchpad RAM
 */
#if defined(CHIP_FAMILY_MEC17XX)
#define MCHP_IMAGETYPE_IDX     31
#elif defined(CHIP_FAMILY_MEC152X)
/* Miscellaneous firmware control fields
 * scratch pad index cannot be more than 32 as
 * MCHP 1501 has 64 bytes = 16 indexes of scratchpad RAM
 */
#define MCHP_IMAGETYPE_IDX     15

#endif 

/* LPC */
#define MCHP_LPC_CFG_BASE     0x400f3300
#define MCHP_LPC_ACT          REG8(MCHP_LPC_CFG_BASE + 0x30)
#define MCHP_LPC_SIRQ(x)      REG8(MCHP_LPC_CFG_BASE + 0x40 + (x))
#define MCHP_LPC_CFG_BAR      REG32(MCHP_LPC_CFG_BASE + 0x60)
#define MCHP_LPC_MAILBOX_BAR  REG32(MCHP_LPC_CFG_BASE + 0x64)
#define MCHP_LPC_8042_BAR     REG32(MCHP_LPC_CFG_BASE + 0x68)
#define MCHP_LPC_ACPI_EC0_BAR REG32(MCHP_LPC_CFG_BASE + 0x6C)
#define MCHP_LPC_ACPI_EC1_BAR REG32(MCHP_LPC_CFG_BASE + 0x70)
#define MCHP_LPC_ACPI_EC2_BAR REG32(MCHP_LPC_CFG_BASE + 0x74)
#define MCHP_LPC_ACPI_EC3_BAR REG32(MCHP_LPC_CFG_BASE + 0x78)
#define MCHP_LPC_ACPI_EC4_BAR REG32(MCHP_LPC_CFG_BASE + 0x7C)
#define MCHP_LPC_ACPI_PM1_BAR REG32(MCHP_LPC_CFG_BASE + 0x80)
#define MCHP_LPC_PORT92_BAR   REG32(MCHP_LPC_CFG_BASE + 0x84)
#define MCHP_LPC_UART0_BAR    REG32(MCHP_LPC_CFG_BASE + 0x88)
#define MCHP_LPC_UART1_BAR    REG32(MCHP_LPC_CFG_BASE + 0x8C)
#define MCHP_LPC_EMI0_BAR     REG32(MCHP_LPC_CFG_BASE + 0x90)
#define MCHP_LPC_EMI1_BAR     REG32(MCHP_LPC_CFG_BASE + 0x94)
#define MCHP_LPC_EMI2_BAR     REG32(MCHP_LPC_CFG_BASE + 0x98)
#define MCHP_LPC_P80DBG0_BAR  REG32(MCHP_LPC_CFG_BASE + 0x9C)
#define MCHP_LPC_P80DBG1_BAR  REG32(MCHP_LPC_CFG_BASE + 0xA0)
#define MCHP_LPC_RTC_BAR      REG32(MCHP_LPC_CFG_BASE + 0xA4)

#define MCHP_LPC_ACPI_EC_BAR(x) REG32(MCHP_LPC_CFG_BASE + 0x6C + ((x)<<2))

/* LPC BAR bits */
#define MCHP_LPC_IO_BAR_ADDR_BITPOS	(16)
#define MCHP_LPC_IO_BAR_EN		(1ul << 15)

/* LPC Generic Memory BAR's, 64-bit registers */
#define MCHP_LPC_SRAM0_BAR_LO    REG32(MCHP_LPC_CFG_BASE + 0xB0)
#define MCHP_LPC_SRAM0_BAR_HI    REG32(MCHP_LPC_CFG_BASE + 0xB4)
#define MCHP_LPC_SRAM1_BAR_LO    REG32(MCHP_LPC_CFG_BASE + 0xB8)
#define MCHP_LPC_SRAM1_BAR_HI    REG32(MCHP_LPC_CFG_BASE + 0xBC)

/*
 * LPC Logical Device Memory BAR's, 48-bit registers
 * Use 16-bit aligned access
 */
#define MCHP_LPC_MAILBOX_MEM_BAR_H0  REG32(MCHP_LPC_CFG_BASE + 0xC0)
#define MCHP_LPC_MAILBOX_MEM_BAR_H1  REG32(MCHP_LPC_CFG_BASE + 0xC2)
#define MCHP_LPC_MAILBOX_MEM_BAR_H2  REG32(MCHP_LPC_CFG_BASE + 0xC4)
#define MCHP_LPC_ACPI_EC0_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xC6)
#define MCHP_LPC_ACPI_EC0_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xC8)
#define MCHP_LPC_ACPI_EC0_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xCA)
#define MCHP_LPC_ACPI_EC1_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xCC)
#define MCHP_LPC_ACPI_EC1_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xCE)
#define MCHP_LPC_ACPI_EC1_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xD0)
#define MCHP_LPC_ACPI_EC2_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xD2)
#define MCHP_LPC_ACPI_EC2_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xD4)
#define MCHP_LPC_ACPI_EC2_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xD6)
#define MCHP_LPC_ACPI_EC3_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xD8)
#define MCHP_LPC_ACPI_EC3_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xDA)
#define MCHP_LPC_ACPI_EC3_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xDC)
#define MCHP_LPC_ACPI_EC4_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xDE)
#define MCHP_LPC_ACPI_EC4_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xE0)
#define MCHP_LPC_ACPI_EC4_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xE2)
#define MCHP_LPC_ACPI_EMI0_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xE4)
#define MCHP_LPC_ACPI_EMI0_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xE6)
#define MCHP_LPC_ACPI_EMI0_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xE8)
#define MCHP_LPC_ACPI_EMI1_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xEA)
#define MCHP_LPC_ACPI_EMI1_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xEC)
#define MCHP_LPC_ACPI_EMI1_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xEE)
#define MCHP_LPC_ACPI_EMI2_MEM_BAR_H0 REG32(MCHP_LPC_CFG_BASE + 0xF0)
#define MCHP_LPC_ACPI_EMI2_MEM_BAR_H1 REG32(MCHP_LPC_CFG_BASE + 0xF2)
#define MCHP_LPC_ACPI_EMI2_MEM_BAR_H2 REG32(MCHP_LPC_CFG_BASE + 0xF4)


#define MCHP_LPC_RT_BASE		0x400f3100
#define MCHP_LPC_BUS_MONITOR		REG32(MCHP_LPC_RT_BASE + 0x4)
#define MCHP_LPC_HOST_ERROR		REG32(MCHP_LPC_RT_BASE + 0x8)
#define MCHP_LPC_EC_SERIRQ		REG32(MCHP_LPC_RT_BASE + 0xC)
#define MCHP_LPC_EC_CLK_CTRL		REG32(MCHP_LPC_RT_BASE + 0x10)
#define MCHP_LPC_BAR_INHIBIT		REG32(MCHP_LPC_RT_BASE + 0x20)
#define MCHP_LPC_BAR_INIT		REG32(MCHP_LPC_RT_BASE + 0x30)
#define MCHP_LPC_SRAM0_BAR		REG32(MCHP_LPC_RT_BASE + 0xf8)
#define MCHP_LPC_SRAM1_BAR		REG32(MCHP_LPC_RT_BASE + 0xfc)


/* EMI */
#define MCHP_EMI_BASE(x)     (0x400F4100 + ((x) << 10))
#define MCHP_EMI_H2E_MBX(x)  REG8(MCHP_EMI_BASE(x) + 0x0)
#define MCHP_EMI_E2H_MBX(x)  REG8(MCHP_EMI_BASE(x) + 0x1)
#define MCHP_EMI_MBA0(x)     REG32(MCHP_EMI_BASE(x) + 0x4)
#define MCHP_EMI_MRL0(x)     REG16(MCHP_EMI_BASE(x) + 0x8)
#define MCHP_EMI_MWL0(x)     REG16(MCHP_EMI_BASE(x) + 0xa)
#define MCHP_EMI_MBA1(x)     REG32(MCHP_EMI_BASE(x) + 0xc)
#define MCHP_EMI_MRL1(x)     REG16(MCHP_EMI_BASE(x) + 0x10)
#define MCHP_EMI_MWL1(x)     REG16(MCHP_EMI_BASE(x) + 0x12)
#define MCHP_EMI_ISR(x)      REG16(MCHP_EMI_BASE(x) + 0x14)
#define MCHP_EMI_HCE(x)      REG16(MCHP_EMI_BASE(x) + 0x16)

#define MCHP_EMI_RT_BASE(x)  (0x400F4000 + ((x) << 10))
#define MCHP_EMI_ISR_B0(x)   REG8(MCHP_EMI_RT_BASE(x) + 0x8)
#define MCHP_EMI_ISR_B1(x)   REG8(MCHP_EMI_RT_BASE(x) + 0x9)
#define MCHP_EMI_IMR_B0(x)   REG8(MCHP_EMI_RT_BASE(x) + 0xa)
#define MCHP_EMI_IMR_B1(x)   REG8(MCHP_EMI_RT_BASE(x) + 0xb)
/*
 * EMI[0:2] on GIRQ15 bits[2:4]
 */
#define MCHP_EMI_GIRQ	15
#define MCHP_EMI_GIRQ_BIT(x)	(1ul << ((x)+2))


/* Mailbox */
#define MCHP_MBX_RT_BASE    0x400f0000
#define MCHP_MBX_INDEX      REG8(MCHP_MBX_RT_BASE + 0x0)
#define MCHP_MBX_DATA       REG8(MCHP_MBX_RT_BASE + 0x1)

#define MCHP_MBX_BASE       0x400f0100
#define MCHP_MBX_H2E_MBX    REG8(MCHP_MBX_BASE + 0x0)
#define MCHP_MBX_E2H_MBX    REG8(MCHP_MBX_BASE + 0x4)
#define MCHP_MBX_ISR        REG8(MCHP_MBX_BASE + 0x8)
#define MCHP_MBX_IMR        REG8(MCHP_MBX_BASE + 0xc)
#define MCHP_MBX_REG(x)     REG8(MCHP_MBX_BASE + 0x10 + (x))
/*
 * Mailbox on GIRQ15 bit[20]
 */
#define MCHP_MBX_GIRQ		15
#define MCHP_MBX_GIRQ_BIT	(1ul << 20)


/* Port80 Capture */
#define MCHP_P80_BASE(x)	(0x400f8000 + ((x) << 10))
#define MCHP_P80_HOST_DATA(x)	REG8(MCHP_P80_BASE(x))
/* Data catpure with timestamp register */
#define MCHP_P80_CAP(x)		REG32(MCHP_P80_BASE(x) + 0x100)
#define MCHP_P80_CFG(x)		REG8(MCHP_P80_BASE(x) + 0x104)
#define MCHP_P80_STS(x)		REG8(MCHP_P80_BASE(x) + 0x108)
#define MCHP_P80_CNT(x)		REG32(MCHP_P80_BASE(x) + 0x10c)
#define MCHP_P80_CNT_GET(x)	(REG32(MCHP_P80_BASE(x) + 0x10c) >> 8)
#define MCHP_P80_CNT_SET(x, c)	(REG32(MCHP_P80_BASE(x) + 0x10c) = (c << 8))
#define MCHP_P80_ACTIVATE(x)	REG8(MCHP_P80_BASE(x) + 0x330)
/*
 * Port80 [0:1] GIRQ15 bits[22:23]
 */
#define MCHP_P80_GIRQ		15
#define MCHP_P80_GIRQ_BIT(x)	(1ul << ((x) + 22))

/* Port80 Data register bits
 * bits[7:0] = data captured on Host write
 * bits[31:8] = optional time stamp
 */
#define MCHP_P80_CAP_DATA_MASK	0xFFul
#define MCHP_P80_CAP_TS_BITPOS	8
#define MCHP_P80_CAP_TS_MASK0	0xfffffful
#define MCHP_P80_CAP_TS_MASK	(\
	(MCHP_P80_CAP_TS_MASK0) << (MCHP_P80_CAP_TS_BITPOS))

/* Port80 Configuration register bits */
#define MCHP_P80_FLUSH_FIFO_WO		(1u << 1)
#define MCHP_P80_RESET_TIMESTAMP_WO	(1u << 2)
#define MCHP_P80_TIMEBASE_BITPOS	3
#define MCHP_P80_TIMEBASE_MASK0		0x03
#define MCHP_P80_TIMEBASE_MASK		(\
	(MCHP_P80_TIMEBASE_MASK0) << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_750KHZ	(\
	0x03 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_1500KHZ	(\
	0x02 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_3MHZ		(\
	0x01 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_6MHZ		(\
	0x00 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMER_ENABLE		(1u << 5)
#define MCHP_P80_FIFO_THRHOLD_MASK	(3u << 6)
#define MCHP_P80_FIFO_THRHOLD_1		(0u << 6)
#define MCHP_P80_FIFO_THRHOLD_4		(1u << 6)
#define MCHP_P80_FIFO_THRHOLD_8		(2u << 6)
#define MCHP_P80_FIFO_THRHOLD_14	(3u << 6)
#define MCHP_P80_FIFO_LEN		16

/* Port80 Status register bits, read-only */
#define MCHP_P80_STS_NOT_EMPTY		0x01
#define MCHP_P80_STS_OVERRUN		0x02

/* Port80 Count register bits */
#define MCHP_P80_CNT_BITPOS		8
#define MCHP_P80_CNT_MASK0		0xfffffful
#define MCHP_P80_CNT_MASK	((MCHP_P80_CNT_MASK0) <<\
	(MCHP_P80_CNT_BITPOS))


/* PWM */
#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_PWM_ID_MAX		(9)
#elif defined(CHIP_FAMILY_MEC17XX)
#define MCHP_PWM_ID_MAX		(12)
#endif 
#define MCHP_PWM_BASE(x)	(0x40005800 + ((x) << 4))
#define MCHP_PWM_ON(x)		REG32(MCHP_PWM_BASE(x) + 0x00)
#define MCHP_PWM_OFF(x)		REG32(MCHP_PWM_BASE(x) + 0x04)
#define MCHP_PWM_CFG(x)		REG32(MCHP_PWM_BASE(x) + 0x08)

/* TACH */
#define MCHP_TACH_ID_MAX		(3)
#define MCHP_TACH_BASE(x)	(0x40006000 + ((x) << 4))
#define MCHP_TACH_CTRL(x)	REG32(MCHP_TACH_BASE(x))
#define MCHP_TACH_CTRL_LO(x)	REG16(MCHP_TACH_BASE(x) + 0x00)
#define MCHP_TACH_CTRL_CNT(x)	REG16(MCHP_TACH_BASE(x) + 0x02)
#define MCHP_TACH_STATUS(x)	REG8(MCHP_TACH_BASE(x) + 0x04)
#define MCHP_TACH_LIMIT_HI(x)	REG16(MCHP_TACH_BASE(x) + 0x08)
#define MCHP_TACH_LIMIT_LO(x)	REG16(MCHP_TACH_BASE(x) + 0x0C)
#define MCHP_TACH_CTRL_OUT_OF_LIM_EN	BIT(0)
#define MCHP_TACH_CTRL_ENABLE			BIT(1)
#define MCHP_TACH_CTRL_FILTER_EN		BIT(8)
#define MCHP_TACH_CTRL_MODE_SELECT		BIT(10)
#define MCHP_TACH_CTRL_TACH_EDGES_2		(0<<11)
#define MCHP_TACH_CTRL_TACH_EDGES_3		(1<<11)
#define MCHP_TACH_CTRL_TACH_EDGES_5		(2<<11)
#define MCHP_TACH_CTRL_TACH_EDGES_9		(3<<11)


/*
 * TACH [2:0] GIRQ17 bits[3:1]
 */
#define MCHP_TACH_GIRQ		17
#define MCHP_TACH_GIRQ_BIT(x)	(1ul << ((x) + 1))


/* ACPI */
#if defined(CHIP_FAMILY_MEC17XX)
#define MCHP_ACPI_EC_MAX		(5)
#elif defined(CHIP_FAMILY_MEC152X)
#define MCHP_ACPI_EC_MAX		(4)
#else 
#error "BUILD ERROR: CHIP_FAMILY_MEC17XX or CHIP_FAMILY_MEC152X not defined!"
#endif 

#define MCHP_ACPI_EC_BASE(x)		(0x400f0800 + ((x) << 10))
#define MCHP_ACPI_EC_EC2OS(x, y)	REG8(MCHP_ACPI_EC_BASE(x) +\
	0x100 + (y))
#define MCHP_ACPI_EC_STATUS(x)		REG8(MCHP_ACPI_EC_BASE(x) + 0x104)
#define MCHP_ACPI_EC_BYTE_CTL(x)	REG8(MCHP_ACPI_EC_BASE(x) + 0x105)
#define MCHP_ACPI_EC_OS2EC(x, y)	REG8(MCHP_ACPI_EC_BASE(x) +\
	0x108 + (y))

#define MCHP_ACPI_PM_RT_BASE     0x400f1c00
#define MCHP_ACPI_PM1_STS1       REG8(MCHP_ACPI_PM_RT_BASE + 0x0)
#define MCHP_ACPI_PM1_STS2       REG8(MCHP_ACPI_PM_RT_BASE + 0x1)
#define MCHP_ACPI_PM1_EN1        REG8(MCHP_ACPI_PM_RT_BASE + 0x2)
#define MCHP_ACPI_PM1_EN2        REG8(MCHP_ACPI_PM_RT_BASE + 0x3)
#define MCHP_ACPI_PM1_CTL1       REG8(MCHP_ACPI_PM_RT_BASE + 0x4)
#define MCHP_ACPI_PM1_CTL2       REG8(MCHP_ACPI_PM_RT_BASE + 0x5)
#define MCHP_ACPI_PM2_CTL1       REG8(MCHP_ACPI_PM_RT_BASE + 0x6)
#define MCHP_ACPI_PM2_CTL2       REG8(MCHP_ACPI_PM_RT_BASE + 0x7)
#define MCHP_ACPI_PM_EC_BASE     0x400f1d00
#define MCHP_ACPI_PM_STS         REG8(MCHP_ACPI_PM_EC_BASE + 0x10)
/*
 * All ACPI EC controllers connected to GIRQ15
 * ACPI EC[0:4] IBF = GIRQ15 bits[5,7,9,11,13]
 * ACPI EC[0:4] OBE = GIRQ15 bits[6,8,10,12,14]
 * ACPI PM1_CTL = GIRQ15 bit[15]
 * ACPI PM1_EN  = GIRQ15 bit[16]
 * ACPI PM1_STS = GIRQ16 bit[17]
 */
#define MCHP_ACPI_EC_GIRQ		15
#define MCHP_ACPI_EC_IBF_GIRQ_BIT(x)	(1ul << (((x)<<1) + 5))
#define MCHP_ACPI_EC_OBE_GIRQ_BIT(x)	(1ul << (((x)<<1) + 6))
#define MCHP_ACPI_PM1_CTL_GIRQ_BIT	15
#define MCHP_ACPI_PM1_EN_GIRQ_BIT	16
#define MCHP_ACPI_PM1_STS_GIRQ_BIT	17

/* 8042 */
#define MCHP_8042_BASE      0x400f0400
#define MCHP_8042_OBF_CLR   REG8(MCHP_8042_BASE + 0x0)
#define MCHP_8042_STATUS    REG8(MCHP_8042_BASE + 0x4)
#define MCHP_8042_H2E       REG8(MCHP_8042_BASE + 0x100)
#define MCHP_8042_E2H       REG8(MCHP_8042_BASE + 0x100)
#define MCHP_8042_STS       REG8(MCHP_8042_BASE + 0x104)
#define MCHP_8042_KB_CTRL   REG8(MCHP_8042_BASE + 0x108)
#define MCHP_8042_AUX_E2H	REG8(MCHP_8042_BASE + 0x10C)
#define MCHP_8042_PCOBF     REG8(MCHP_8042_BASE + 0x114)
#define MCHP_8042_ACT       REG8(MCHP_8042_BASE + 0x330)
/*
 * 8042 [OBE:IBF] = GIRQ15 bits[18:19]
 */
#define MCHP_8042_GIRQ		15
#define MCHP_8042_OBE_GIRQ_BIT	(1ul << 18)
#define MCHP_8042_IBF_GIRQ_BIT	(1ul << 19)

/* FAN */
#define MCHP_FAN_BASE(x)         (0x4000a000 + ((x) << 7))
#define MCHP_FAN_SETTING(x)      REG8(MCHP_FAN_BASE(x) + 0x0)
#define MCHP_FAN_PWM_DIVIDE(x)   REG8(MCHP_FAN_BASE(x) + 0x1)
#define MCHP_FAN_CFG1(x)         REG8(MCHP_FAN_BASE(x) + 0x2)
#define MCHP_FAN_CFG2(x)         REG8(MCHP_FAN_BASE(x) + 0x3)
#define MCHP_FAN_GAIN(x)         REG8(MCHP_FAN_BASE(x) + 0x5)
#define MCHP_FAN_SPIN_UP(x)      REG8(MCHP_FAN_BASE(x) + 0x6)
#define MCHP_FAN_STEP(x)         REG8(MCHP_FAN_BASE(x) + 0x7)
#define MCHP_FAN_MIN_DRV(x)      REG8(MCHP_FAN_BASE(x) + 0x8)
#define MCHP_FAN_VALID_CNT(x)    REG8(MCHP_FAN_BASE(x) + 0x9)
#define MCHP_FAN_DRV_FAIL(x)     REG16(MCHP_FAN_BASE(x) + 0xa)
#define MCHP_FAN_TARGET(x)       REG16(MCHP_FAN_BASE(x) + 0xc)
#define MCHP_FAN_READING(x)      REG16(MCHP_FAN_BASE(x) + 0xe)
#define MCHP_FAN_BASE_FREQ(x)    REG8(MCHP_FAN_BASE(x) + 0x10)
#define MCHP_FAN_STATUS(x)       REG8(MCHP_FAN_BASE(x) + 0x11)
/*
 * FAN(RPM2PWM) all instances on GIRQ17
 * FAN[0:1] Fail  = GIRQ17 bits[4,6]
 * FAN[0:1] Stall = GIRQ17 bits[5,7]
 */
#define MCHP_FAN_GIRQ			17
#define MCHP_FAN_FAIL_GIRQ_BIT(x)	(1ul << (((x)<<1)+4))
#define MCHP_FAN_STALL_GIRQ_BIT(x)	(1ul << (((x)<<1)+5))


/* I2C */
#define MCHP_I2C_CTRL0		(0)
#define MCHP_I2C_CTRL1		(1)
#define MCHP_I2C_CTRL2		(2)
#define MCHP_I2C_CTRL3		(3)
#ifdef CHIP_FAMILY_MEC152X
#define MCHP_I2C_CTRL4		(4)
#define MCHP_I2C_CTRL_MAX	(5)
#else
#define MCHP_I2C_CTRL_MAX	(4)
#endif

#define MCHP_I2C_BASE(x)	(0x40004000 + ((x) << 10))
#define MCHP_I2C0_BASE		0x40004000
#define MCHP_I2C1_BASE		0x40004400
#define MCHP_I2C2_BASE		0x40004800
#define MCHP_I2C3_BASE		0x40004C00
#define MCHP_I2C_BASESEP	0x00000400
#define MCHP_I2C_ADDR(controller, offset) \
	(offset + MCHP_I2C_BASE(controller))

/*
 * MEC1701H 144-pin package has four I2C controller and eleven ports.
 * Any port can be mapped to any I2C controller.
 * This package does not implement pins for I2C01 (Port 1).
 *
 * I2C port values must be zero based consecutive whole numbers due to
 * port number used as an index for I2C mutex array, etc.
 *
 * Refer to chip i2c_port_to_controller function for mapping
 * of port to controller.
 *
 * Locking must occur by-controller (not by-port).
 */
#define MCHP_I2C_PORT_MASK	0x07FDul

enum MCHP_i2c_port {
	MCHP_I2C_PORT0 = 0,
	MCHP_I2C_PORT1,	/* port 1, do not use. pins not present */
	MCHP_I2C_PORT2,
	MCHP_I2C_PORT3,
	MCHP_I2C_PORT4,
	MCHP_I2C_PORT5,
	MCHP_I2C_PORT6,
	MCHP_I2C_PORT7,
	MCHP_I2C_PORT8,
	MCHP_I2C_PORT9,
	MCHP_I2C_PORT10,

	MCHP_I2C_PORT_COUNT,
};

#define MCHP_I2C_CTRL(ctrl)          REG8(MCHP_I2C_ADDR(ctrl, 0x0))
#define MCHP_I2C_STATUS(ctrl)        REG8(MCHP_I2C_ADDR(ctrl, 0x0))
#define MCHP_I2C_OWN_ADDR(ctrl)      REG16(MCHP_I2C_ADDR(ctrl, 0x4))
#define MCHP_I2C_DATA(ctrl)          REG8(MCHP_I2C_ADDR(ctrl, 0x8))
#define MCHP_I2C_MASTER_CMD(ctrl)    REG32(MCHP_I2C_ADDR(ctrl, 0xc))
#define MCHP_I2C_SLAVE_CMD(ctrl)     REG32(MCHP_I2C_ADDR(ctrl, 0x10))
#define MCHP_I2C_PEC(ctrl)           REG8(MCHP_I2C_ADDR(ctrl, 0x14))
#define MCHP_I2C_DATA_TIM_2(ctrl)    REG8(MCHP_I2C_ADDR(ctrl, 0x18))
#define MCHP_I2C_COMPLETE(ctrl)      REG32(MCHP_I2C_ADDR(ctrl, 0x20))
#define MCHP_I2C_IDLE_SCALE(ctrl)    REG32(MCHP_I2C_ADDR(ctrl, 0x24))
#define MCHP_I2C_CONFIG(ctrl)        REG32(MCHP_I2C_ADDR(ctrl, 0x28))
#define MCHP_I2C_BUS_CLK(ctrl)       REG16(MCHP_I2C_ADDR(ctrl, 0x2c))
#define MCHP_I2C_BLK_ID(ctrl)        REG8(MCHP_I2C_ADDR(ctrl, 0x30))
#define MCHP_I2C_REV(ctrl)           REG8(MCHP_I2C_ADDR(ctrl, 0x34))
#define MCHP_I2C_BB_CTRL(ctrl)       REG8(MCHP_I2C_ADDR(ctrl, 0x38))
#define MCHP_I2C_DATA_TIM(ctrl)      REG32(MCHP_I2C_ADDR(ctrl, 0x40))
#define MCHP_I2C_TOUT_SCALE(ctrl)    REG32(MCHP_I2C_ADDR(ctrl, 0x44))
#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_I2C_SLAVE_TX_BUF(ctrl)  REG8(MCHP_I2C_ADDR(ctrl, 0x48))
#define MCHP_I2C_SLAVE_RX_BUF(ctrl)  REG8(MCHP_I2C_ADDR(ctrl, 0x4c))
#define MCHP_I2C_MASTER_TX_BUF(ctrl) REG8(MCHP_I2C_ADDR(ctrl, 0x50))
#define MCHP_I2C_MASTER_RX_BUF(ctrl) REG8(MCHP_I2C_ADDR(ctrl, 0x54))
#endif
#define MCHP_I2C_WAKE_STS(ctrl)      REG8(MCHP_I2C_ADDR(ctrl, 0x60))
#define MCHP_I2C_WAKE_EN(ctrl)       REG8(MCHP_I2C_ADDR(ctrl, 0x64))
#ifdef CHIP_FAMILY_MEC152X
#define MCHP_I2C_SLAVE_ADDR(ctrl)    REG32(MCHP_I2C_ADDR(ctrl, 0x6C))
#define MCHP_I2C_PROM_INT(ctrl)      REG32(MCHP_I2C_ADDR(ctrl, 0x70))
#define MCHP_I2C_PROM_INT_EN(ctrl)   REG32(MCHP_I2C_ADDR(ctrl, 0x74))
#define MCHP_I2C_PROM_CTRL(ctrl)     REG32(MCHP_I2C_ADDR(ctrl, 0x78))


#endif
/* All I2C controller connected to GIRQ13 */
#define MCHP_I2C_GIRQ			13
/* I2C[0:3] -> GIRQ13 bits[0:3] */
#define MCHP_I2C_GIRQ_BIT(x)		(1ul << (x))


/* Keyboard scan matrix */
#define MCHP_KS_BASE		0x40009c00
#define MCHP_KS_KSO_SEL		REG32(MCHP_KS_BASE + 0x4)
#define MCHP_KS_KSI_INPUT	REG32(MCHP_KS_BASE + 0x8)
#define MCHP_KS_KSI_STATUS	REG32(MCHP_KS_BASE + 0xc)
#define MCHP_KS_KSI_INT_EN	REG32(MCHP_KS_BASE + 0x10)
#define MCHP_KS_EXT_CTRL	REG32(MCHP_KS_BASE + 0x14)
#define MCHP_KS_GIRQ		21
#define MCHP_KS_GIRQ_BIT	(1ul << 25)


/* ADC */
#define MCHP_ADC_BASE       0x40007c00
#define MCHP_ADC_CTRL       REG32(MCHP_ADC_BASE + 0x0)
#define MCHP_ADC_DELAY      REG32(MCHP_ADC_BASE + 0x4)
#define MCHP_ADC_STS        REG32(MCHP_ADC_BASE + 0x8)
#define MCHP_ADC_SINGLE     REG32(MCHP_ADC_BASE + 0xc)
#define MCHP_ADC_REPEAT     REG32(MCHP_ADC_BASE + 0x10)
#define MCHP_ADC_READ(x)    REG32(MCHP_ADC_BASE + 0x14 + ((x) * 0x4))

#define MCHP_ADC_GIRQ			17
#define MCHP_ADC_GIRQ_SINGLE_BIT	(1ul << 8)
#define MCHP_ADC_GIRQ_REPEAT_BIT	(1ul << 9)


/* Hibernation timer */
#define MCHP_HTIMER_BASE(x)     (0x40009800 + ((x) << 5))
#define MCHP_HTIMER_PRELOAD(x)  REG16(MCHP_HTIMER_BASE(x) + 0x0)
#define MCHP_HTIMER_CONTROL(x)  REG16(MCHP_HTIMER_BASE(x) + 0x4)
#define MCHP_HTIMER_COUNT(x)    REG16(MCHP_HTIMER_BASE(x) + 0x8)
/* All Hibernation timers connected to GIRQ21 */
#define MCHP_HTIMER_GIRQ	21
/* HTIMER[0:1] -> GIRQ21 bits[1:2] */
#define MCHP_HTIMER_GIRQ_BIT(x)	(1ul << ((x) + 1))

/* VBAT-Powered Control Interface */
#define MCHP_VCI_BASE      0x4000ae00
#define MCHP_VCI_REGISTER   REG32(MCHP_VCI_BASE + 0x00)
#define MCHP_VCI_LATCH_ENABLE   REG32(MCHP_VCI_BASE + 0x04)
#define MCHP_VCI_LATCH_RESET   	REG32(MCHP_VCI_BASE + 0x08)
#define MCHP_VCI_INPUT_ENABLE   REG32(MCHP_VCI_BASE + 0x0c)
#define MCHP_VCI_POLARITY		REG32(MCHP_VCI_BASE + 0x14)
#define MCHP_VCI_POSEDGE_DETECT REG32(MCHP_VCI_BASE + 0x18)
#define MCHP_VCI_NEGEDGE_DETECT REG32(MCHP_VCI_BASE + 0x1C)
#define MCHP_VCI_BUFFER_EN		REG32(MCHP_VCI_BASE + 0x20)

#define MCHP_VCI_REGISTER_FW_CNTRL	BIT(10)
#define MCHP_VCI_REGISTER_FW_EXT	BIT(11)

/* General Purpose SPI (GP-SPI) */
#define MCHP_SPI_BASE(port) (0x40009400 + ((port) << 7))
#define MCHP_SPI_AR(port)   REG8(MCHP_SPI_BASE(port) + 0x00)
#define MCHP_SPI_CR(port)   REG8(MCHP_SPI_BASE(port) + 0x04)
#define MCHP_SPI_SR(port)   REG8(MCHP_SPI_BASE(port) + 0x08)
#define MCHP_SPI_TD(port)   REG8(MCHP_SPI_BASE(port) + 0x0c)
#define MCHP_SPI_RD(port)   REG8(MCHP_SPI_BASE(port) + 0x10)
#define MCHP_SPI_CC(port)   REG8(MCHP_SPI_BASE(port) + 0x14)
#define MCHP_SPI_CG(port)   REG8(MCHP_SPI_BASE(port) + 0x18)
/* Addresses of TX/RX register used in tables */
#define MCHP_SPI_TD_ADDR(ctrl)	(MCHP_SPI_BASE(ctrl) + 0x0c)
#define MCHP_SPI_RD_ADDR(ctrl)	(MCHP_SPI_BASE(ctrl) + 0x10)
/* All GP-SPI controllers connected to GIRQ18 */
#define MCHP_SPI_GIRQ		18
/*
 * SPI[0:1] TXBE -> GIRQ18 bits[2,4]
 * SPI[0:1] RXBF -> GIRQ18 bits[3,5]
 */
#define MCHP_SPI_GIRQ_TXBE_BIT(x)	(1ul << (((x) << 1) + 2))
#define MCHP_SPI_GIRQ_RXBF_BIT(x)	(1ul << (((x) << 1) + 3))

#define MCHP_GPSPI0_ID	0
#define MCHP_GPSPI1_ID	1

#if defined(CHIP_FAMILY_MEC17XX) || defined(CHIP_FAMILY_MEC152X)

/* Quad Master SPI (QMSPI) */
#if defined(CHIP_FAMILY_MEC17XX) 
#define MCHP_QMSPI0_BASE		0x40005400
#elif defined(CHIP_FAMILY_MEC152X)
#define MCHP_QMSPI0_BASE		0x40070000
#endif 

#define MCHP_QMSPI0_MODE		REG32(MCHP_QMSPI0_BASE + 0x00)
#define MCHP_QMSPI0_MODE_ACT_SRST	REG8(MCHP_QMSPI0_BASE + 0x00)
#define MCHP_QMSPI0_MODE_SPI_MODE	REG8(MCHP_QMSPI0_BASE + 0x01)
#define MCHP_QMSPI0_MODE_FDIV		REG8(MCHP_QMSPI0_BASE + 0x02)
#define MCHP_QMSPI0_CTRL		REG32(MCHP_QMSPI0_BASE + 0x04)
#define MCHP_QMSPI0_EXE			REG8(MCHP_QMSPI0_BASE + 0x08)
#define MCHP_QMSPI0_IFCTRL		REG8(MCHP_QMSPI0_BASE + 0x0C)
#define MCHP_QMSPI0_STS			REG32(MCHP_QMSPI0_BASE + 0x10)
#define MCHP_QMSPI0_BUFCNT_STS		REG32(MCHP_QMSPI0_BASE + 0x14)
#define MCHP_QMSPI0_IEN			REG32(MCHP_QMSPI0_BASE + 0x18)
#define MCHP_QMSPI0_BUFCNT_TRIG		REG32(MCHP_QMSPI0_BASE + 0x1C)
#define MCHP_QMSPI0_TX_FIFO_ADDR	(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_TX_FIFO8		REG8(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_TX_FIFO16		REG16(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_TX_FIFO32		REG32(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_RX_FIFO_ADDR	(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_RX_FIFO8		REG8(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_RX_FIFO16		REG16(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_RX_FIFO32		REG32(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_DESCR(x)		REG32(MCHP_QMSPI0_BASE +\
	0x30 + ((x)<<2))
#define MCHP_QMSPI_GIRQ			18
#define MCHP_QMSPI_GIRQ_BIT		(1ul << 1)

#if defined(CHIP_FAMILY_MEC17XX) 
#define MCHP_QMSPI_MAX_DESCR		5
#elif defined(CHIP_FAMILY_MEC152X)
#define MCHP_QMSPI_MAX_DESCR		16
#endif 

/* Bits in MCHP_QMSPI0_MODE */
#define MCHP_QMSPI_M_ACTIVATE        (1ul << 0)
#define MCHP_QMSPI_M_SOFT_RESET      (1ul << 1)
#define MCHP_QMSPI_M_SPI_MODE_MASK   (0x7ul << 8)
#define MCHP_QMSPI_M_SPI_MODE0       (0x0ul << 8)
#define MCHP_QMSPI_M_SPI_MODE3       (0x3ul << 8)
#define MCHP_QMSPI_M_SPI_MODE0_48M   (0x4ul << 8)
#define MCHP_QMSPI_M_SPI_MODE3_48M   (0x7ul << 8)
/*
 * clock divider is 8-bit field in bits[23:16]
 * [1, 255] -> 48MHz / [1, 255], 0 -> 48MHz / 256
 */
#define MCHP_QMSPI_M_CLKDIV_BITPOS	16
#define MCHP_QMSPI_M_CLKDIV_48M		(1ul << 16)
#define MCHP_QMSPI_M_CLKDIV_24M		(2ul << 16)
#define MCHP_QMSPI_M_CLKDIV_16M		(3ul << 16)
#define MCHP_QMSPI_M_CLKDIV_12M		(4ul << 16)
#define MCHP_QMSPI_M_CLKDIV_8M		(6ul << 16)
#define MCHP_QMSPI_M_CLKDIV_6M		(8ul << 16)
#define MCHP_QMSPI_M_CLKDIV_1M		(48ul << 16)
#define MCHP_QMSPI_M_CLKDIV_188K	(0x100ul << 16)

/* Bits in MCHP_QMSPI0_CTRL and MCHP_QMSPI_DESCR(x) */
#define MCHP_QMSPI_C_1X			(0ul << 0) /* Full Duplex */
#define MCHP_QMSPI_C_2X			(1ul << 0) /* Dual IO */
#define MCHP_QMSPI_C_4X			(2ul << 0) /* Quad IO */
#define MCHP_QMSPI_C_TX_DIS		(0ul << 2)
#define MCHP_QMSPI_C_TX_DATA		(1ul << 2)
#define MCHP_QMSPI_C_TX_ZEROS		(2ul << 2)
#define MCHP_QMSPI_C_TX_ONES		(3ul << 2)
#define MCHP_QMSPI_C_TX_DMA_DIS		(0ul << 4)
#define MCHP_QMSPI_C_TX_DMA_1B		(1ul << 4)
#define MCHP_QMSPI_C_TX_DMA_2B		(2ul << 4)
#define MCHP_QMSPI_C_TX_DMA_4B		(3ul << 4)
#define MCHP_QMSPI_C_TX_DMA_MASK	(3ul << 4)
#define MCHP_QMSPI_C_RX_DIS		(0ul << 6)
#define MCHP_QMSPI_C_RX_EN		(1ul << 6)
#define MCHP_QMSPI_C_RX_DMA_DIS		(0ul << 7)
#define MCHP_QMSPI_C_RX_DMA_1B		(1ul << 7)
#define MCHP_QMSPI_C_RX_DMA_2B		(2ul << 7)
#define MCHP_QMSPI_C_RX_DMA_4B		(3ul << 7)
#define MCHP_QMSPI_C_RX_DMA_MASK	(3ul << 7)
#define MCHP_QMSPI_C_NO_CLOSE		(0ul << 9)
#define MCHP_QMSPI_C_CLOSE		(1ul << 9)
#define MCHP_QMSPI_C_XFRU_BITS		(0ul << 10)
#define MCHP_QMSPI_C_XFRU_1B		(1ul << 10)
#define MCHP_QMSPI_C_XFRU_4B		(2ul << 10)
#define MCHP_QMSPI_C_XFRU_16B		(3ul << 10)
#define MCHP_QMSPI_C_XFRU_MASK		(3ul << 10)
/* Control */
#define MCHP_QMSPI_C_START_DESCR_BITPOS (12)
#define MCHP_QMSPI_C_START_DESCR_MASK	(0xFul << 12)
#define MCHP_QMSPI_C_DESCR_MODE_EN	(1ul << 16)
/* Descriptors, indicates the current descriptor is the last */
#define MCHP_QMSPI_C_NEXT_DESCR_BITPOS	12
#define MCHP_QMSPI_C_NEXT_DESCR_MASK0	0xFul
#define MCHP_QMSPI_C_NEXT_DESCR_MASK \
	((MCHP_QMSPI_C_NEXT_DESCR_MASK0) << 12)
#define MCHP_QMSPI_C_NXTD(n)		(n << 12)
#define MCHP_QMSPI_C_NEXTD0		(0ul << 12)
#define MCHP_QMSPI_C_NEXTD1		(1ul << 12)
#define MCHP_QMSPI_C_NEXTD2		(2ul << 12)
#define MCHP_QMSPI_C_NEXTD3		(3ul << 12)
#define MCHP_QMSPI_C_NEXTD4		(4ul << 12)
#define MCHP_QMSPI_C_DESCR_LAST		(1ul << 16)
/*
 * Total transfer length is the count in this field
 * scaled by units in MCHP_QMSPI_CTRL_XFRU_xxxx
 */
#define MCHP_QMSPI_C_NUM_UNITS_BITPOS	(17)
#define MCHP_QMSPI_C_MAX_UNITS		(0x7ffful)
#define MCHP_QMSPI_C_NUM_UNITS_MASK0	(0x7ffful)
#define MCHP_QMSPI_C_NUM_UNITS_MASK \
	((MCHP_QMSPI_C_NUM_UNITS_MASK0) << 17)

/* Bits in MCHP_QMSPI0_EXE */
#define MCHP_QMSPI_EXE_START		BIT(0)
#define MCHP_QMSPI_EXE_STOP		BIT(1)
#define MCHP_QMSPI_EXE_CLR_FIFOS	BIT(2)

/* MCHP QMSPI FIFO Sizes */
#define MCHP_QMSPI_TX_FIFO_LEN	8
#define MCHP_QMSPI_RX_FIFO_LEN	8

/* Bits in MCHP_QMSPI0_STS and MCHP_QMSPI0_IEN */
#define MCHP_QMSPI_STS_DONE		(1ul << 0)
#define MCHP_QMSPI_STS_DMA_DONE		(1ul << 1)
#define MCHP_QMSPI_STS_TX_BUFF_ERR	(1ul << 2)
#define MCHP_QMSPI_STS_RX_BUFF_ERR	(1ul << 3)
#define MCHP_QMSPI_STS_PROG_ERR		(1ul << 4)
#define MCHP_QMSPI_STS_TX_BUFF_FULL	(1ul << 8)
#define MCHP_QMSPI_STS_TX_BUFF_EMPTY	(1ul << 9)
#define MCHP_QMSPI_STS_TX_BUFF_REQ	(1ul << 10)
#define MCHP_QMSPI_STS_TX_BUFF_STALL	(1ul << 11) /* status only */
#define MCHP_QMSPI_STS_RX_BUFF_FULL	(1ul << 12)
#define MCHP_QMSPI_STS_RX_BUFF_EMPTY	(1ul << 13)
#define MCHP_QMSPI_STS_RX_BUFF_REQ	(1ul << 14)
#define MCHP_QMSPI_STS_RX_BUFF_STALL	(1ul << 15) /* status only */
#define MCHP_QMSPI_STS_ACTIVE		(1ul << 16) /* status only */

/* Bits in MCHP_QMSPI0_BUFCNT (read-only) */
#define MCHP_QMSPI_BUFCNT_TX_BITPOS	(0)
#define MCHP_QMSPI_BUFCNT_TX_MASK	(0xFFFFul)
#define MCHP_QMSPI_BUFCNT_RX_BITPOS	(16)
#define MCHP_QMSPI_BUFCNT_RX_MASK	(0xFFFFul << 16)

#define MCHP_QMSPI0_ID	0

#endif /* CHIP_FAMILY_MEC17XX || CHIP_FAMILY_MEC152X*/

/* eSPI */

/* eSPI IO Component Base Address */
#define MCHP_ESPI_IO_BASE		0x400f3400

/* Peripheral Channel Registers */
#define MCHP_ESPI_PC_STATUS		REG32(MCHP_ESPI_IO_BASE + 0x114)
#define MCHP_ESPI_PC_IEN		REG32(MCHP_ESPI_IO_BASE + 0x118)
#define MCHP_ESPI_PC_BAR_INHIBIT_LO	REG32(MCHP_ESPI_IO_BASE + 0x120)
#define MCHP_ESPI_PC_BAR_INHIBIT_HI	REG32(MCHP_ESPI_IO_BASE + 0x124)
#define MCHP_ESPI_PC_BAR_INIT_LD_0C	REG16(MCHP_ESPI_IO_BASE + 0x128)
#define MCHP_ESPI_PC_EC_IRQ		REG8(MCHP_ESPI_IO_BASE + 0x12C)

/* LTR Registers */
#define MCHP_ESPI_IO_LTR_STATUS		REG16(MCHP_ESPI_IO_BASE + 0x220)
#define MCHP_ESPI_IO_LTR_IEN		REG8(MCHP_ESPI_IO_BASE + 0x224)
#define MCHP_ESPI_IO_LTR_CTRL		REG16(MCHP_ESPI_IO_BASE + 0x228)
#define MCHP_ESPI_IO_LTR_MSG		REG16(MCHP_ESPI_IO_BASE + 0x22C)

/* OOB Channel Registers */
#define MCHP_ESPI_OOB_RX_ADDR_LO	REG32(MCHP_ESPI_IO_BASE + 0x240)
#define MCHP_ESPI_OOB_RX_ADDR_HI	REG32(MCHP_ESPI_IO_BASE + 0x244)
#define MCHP_ESPI_OOB_TX_ADDR_LO	REG32(MCHP_ESPI_IO_BASE + 0x248)
#define MCHP_ESPI_OOB_TX_ADDR_HI	REG32(MCHP_ESPI_IO_BASE + 0x24C)
#define MCHP_ESPI_OOB_RX_LEN		REG32(MCHP_ESPI_IO_BASE + 0x250)
#define MCHP_ESPI_OOB_TX_LEN		REG32(MCHP_ESPI_IO_BASE + 0x254)
#define MCHP_ESPI_OOB_RX_CTL		REG32(MCHP_ESPI_IO_BASE + 0x258)
#define MCHP_ESPI_OOB_RX_IEN		REG8(MCHP_ESPI_IO_BASE + 0x25C)
#define MCHP_ESPI_OOB_RX_STATUS		REG32(MCHP_ESPI_IO_BASE + 0x260)
#define MCHP_ESPI_OOB_TX_CTL		REG32(MCHP_ESPI_IO_BASE + 0x264)
#define MCHP_ESPI_OOB_TX_IEN		REG8(MCHP_ESPI_IO_BASE + 0x268)
#define MCHP_ESPI_OOB_TX_STATUS		REG32(MCHP_ESPI_IO_BASE + 0x26C)

/* Flash Channel Registers */
#define MCHP_ESPI_FC_ADDR_LO		REG32(MCHP_ESPI_IO_BASE + 0x280)
#define MCHP_ESPI_FC_ADDR_HI		REG32(MCHP_ESPI_IO_BASE + 0x284)
#define MCHP_ESPI_FC_BUF_ADDR_LO	REG32(MCHP_ESPI_IO_BASE + 0x288)
#define MCHP_ESPI_FC_BUF_ADDR_HI	REG32(MCHP_ESPI_IO_BASE + 0x28C)
#define MCHP_ESPI_FC_XFR_LEN		REG32(MCHP_ESPI_IO_BASE + 0x290)
#define MCHP_ESPI_FC_CTL		REG32(MCHP_ESPI_IO_BASE + 0x294)
#define MCHP_ESPI_FC_IEN		REG8(MCHP_ESPI_IO_BASE + 0x298)
#define MCHP_ESPI_FC_CONFIG		REG32(MCHP_ESPI_IO_BASE + 0x29C)
#define MCHP_ESPI_FC_STATUS		REG32(MCHP_ESPI_IO_BASE + 0x2A0)

/* VWire Channel Registers */
#define MCHP_ESPI_VW_STATUS		REG8(MCHP_ESPI_IO_BASE + 0x2B0)

/* Global Registers */
/* 32-bit register containing CAP_ID/CAP0/CAP1/PC_CAP */
#define MCHP_ESPI_IO_REG32_A		REG32(MCHP_ESPI_IO_BASE + 0x2E0)
#define MCHP_ESPI_IO_CAP_ID		REG8(MCHP_ESPI_IO_BASE + 0x2E0)
#define MCHP_ESPI_IO_CAP0		REG8(MCHP_ESPI_IO_BASE + 0x2E1)
#define MCHP_ESPI_IO_CAP1		REG8(MCHP_ESPI_IO_BASE + 0x2E2)
#define MCHP_ESPI_IO_PC_CAP		REG8(MCHP_ESPI_IO_BASE + 0x2E3)
/* 32-bit register containing VW_CAP/OOB_CAP/FC_CAP/PC_READY */
#define MCHP_ESPI_IO_REG32_B		REG32(MCHP_ESPI_IO_BASE + 0x2E4)
#define MCHP_ESPI_IO_VW_CAP		REG8(MCHP_ESPI_IO_BASE + 0x2E4)
#define MCHP_ESPI_IO_OOB_CAP		REG8(MCHP_ESPI_IO_BASE + 0x2E5)
#define MCHP_ESPI_IO_FC_CAP		REG8(MCHP_ESPI_IO_BASE + 0x2E6)
#define MCHP_ESPI_IO_PC_READY		REG8(MCHP_ESPI_IO_BASE + 0x2E7)
/* 32-bit register containing OOB_READY/FC_READY/RESET_STATUS/RESET_IEN */
#define MCHP_ESPI_IO_REG32_C		REG32(MCHP_ESPI_IO_BASE + 0x2E8)
#define MCHP_ESPI_IO_OOB_READY		REG8(MCHP_ESPI_IO_BASE + 0x2E8)
#define MCHP_ESPI_IO_FC_READY		REG8(MCHP_ESPI_IO_BASE + 0x2E9)
#define MCHP_ESPI_IO_RESET_STATUS	REG8(MCHP_ESPI_IO_BASE + 0x2EA)
#define MCHP_ESPI_IO_RESET_IEN		REG8(MCHP_ESPI_IO_BASE + 0x2EB)
/* 32-bit register containing PLTRST_SRC/VW_READY */
#define MCHP_ESPI_IO_REG32_D		REG32(MCHP_ESPI_IO_BASE + 0x2EC)
#define MCHP_ESPI_IO_PLTRST_SRC		REG8(MCHP_ESPI_IO_BASE + 0x2EC)
#define MCHP_ESPI_IO_VW_READY		REG8(MCHP_ESPI_IO_BASE + 0x2ED)


/* Bits in MCHP_ESPI_IO_CAP0 */
#define MCHP_ESPI_CAP0_PC_SUPP		0x01
#define MCHP_ESPI_CAP0_VW_SUPP		0x02
#define MCHP_ESPI_CAP0_OOB_SUPP		0x04
#define MCHP_ESPI_CAP0_FC_SUPP		0x08
#define MCHP_ESPI_CAP0_ALL_CHAN_SUPP	(MCHP_ESPI_CAP0_PC_SUPP | \
						MCHP_ESPI_CAP0_VW_SUPP | \
						MCHP_ESPI_CAP0_OOB_SUPP | \
						MCHP_ESPI_CAP0_FC_SUPP)

/* Bits in MCHP_ESPI_IO_CAP1 */
#define MCHP_ESPI_CAP1_RW_MASK		0x37
#define MCHP_ESPI_CAP1_MAX_FREQ_MASK	0x07
#define MCHP_ESPI_CAP1_MAX_FREQ_20M	0x00
#define MCHP_ESPI_CAP1_MAX_FREQ_25M	0x01
#define MCHP_ESPI_CAP1_MAX_FREQ_33M	0x02
#define MCHP_ESPI_CAP1_MAX_FREQ_50M	0x03
#define MCHP_ESPI_CAP1_MAX_FREQ_66M	0x04
#define MCHP_ESPI_CAP1_IO_BITPOS	4
#define MCHP_ESPI_CAP1_IO_MASK0		0x03
#define MCHP_ESPI_CAP1_IO_MASK		(0x03ul << 4)
#define MCHP_ESPI_CAP1_IO1_VAL		0x00
#define MCHP_ESPI_CAP1_IO12_VAL		0x01
#define MCHP_ESPI_CAP1_IO24_VAL		0x02
#define MCHP_ESPI_CAP1_IO124_VAL	0x03
#define MCHP_ESPI_CAP1_IO1		(0x00 << 4)
#define MCHP_ESPI_CAP1_IO12		(0x01 << 4)
#define MCHP_ESPI_CAP1_IO24		(0x02 << 4)
#define MCHP_ESPI_CAP1_IO124		(0x03 << 4)


/* Bits in MCHP_ESPI_IO_RESET_STATUS and MCHP_ESPI_IO_RESET_IEN */
#define MCHP_ESPI_RST_PIN_MASK	0x02
#define MCHP_ESPI_RST_CHG_STS	1
#define MCHP_ESPI_RST_IEN	1


/* Bits in MCHP_ESPI_IO_PLTRST_SRC */
#define MCHP_ESPI_PLTRST_SRC_VW		0
#define MCHP_ESPI_PLTRST_SRC_PIN	1


/*
 * eSPI Slave Activate Register
 * bit[0] = 0 de-active block is clock-gates
 * bit[0] = 1 block is powered and functional
 */
#define MCHP_ESPI_ACTIVATE	REG8(MCHP_ESPI_IO_BASE + 0x330)


/*
 * IO BAR's starting at offset 0x134
 * b[16]=virtualized R/W
 * b[15:14]=0 reserved RO
 * b[13:8]=Logical Device Number RO
 * b[7:0]=mask
 */
#define MCHP_ESPI_IO_BAR_CTL(x)	REG32(MCHP_ESPI_IO_BASE + \
	0x134 + ((x) << 2))
/* access mask field of eSPI IO BAR Control register */
#define MCHP_ESPI_IO_BAR_CTL_MASK(x) REG8(MCHP_ESPI_IO_BASE + \
	0x134 + ((x) << 2))

/*
 * IO BAR's starting at offset 0x334
 * b[31:16] = I/O address
 * b[15:1]=0 reserved
 * b[0] = valid
 */
#define MCHP_ESPI_IO_BAR(x) REG32(MCHP_ESPI_IO_BASE + 0x334 + ((x) << 2))

#define MCHP_ESPI_IO_BAR_VALID(x)	REG8(MCHP_ESPI_IO_BASE + \
	0x334 + ((x) << 2) + 0)
#define MCHP_ESPI_IO_BAR_ADDR_LSB(x)	REG8(MCHP_ESPI_IO_BASE + \
	0x334 + ((x) << 2) + 2)
#define MCHP_ESPI_IO_BAR_ADDR_MSB(x)	REG8(MCHP_ESPI_IO_BASE + \
	0x334 + ((x) << 2) + 3)
#define MCHP_ESPI_IO_BAR_ADDR(x)	REG16(MCHP_ESPI_IO_BASE + \
	0x334 + ((x) << 2) + 2)

/* Indices for use in above macros */
#define MCHP_ESPI_IO_BAR_ID_CFG_PORT	0
#define MCHP_ESPI_IO_BAR_ID_MEM_CMPNT	1
#define MCHP_ESPI_IO_BAR_ID_MAILBOX	2
#define MCHP_ESPI_IO_BAR_ID_8042	3
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC0	4
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC1	5
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC2	6
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC3	7
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC4	8
#define MCHP_ESPI_IO_BAR_ID_ACPI_PM1	9
#define MCHP_ESPI_IO_BAR_ID_P92		0xA
#define MCHP_ESPI_IO_BAR_ID_UART0	0xB
#define MCHP_ESPI_IO_BAR_ID_UART1	0xC
#define MCHP_ESPI_IO_BAR_ID_EMI0	0xD
#define MCHP_ESPI_IO_BAR_ID_EMI1	0xE
#define MCHP_ESPI_IO_BAR_ID_EMI		0xF
#define MCHP_ESPI_IO_BAR_P80_0		0x10
#define MCHP_ESPI_IO_BAR_P80_1		0x11
#define MCHP_ESPI_IO_BAR_RTC		0x12

/* eSPI Serial IRQ registers */
#define MCHP_ESPI_IO_SERIRQ_REG(x)	REG8(MCHP_ESPI_IO_BASE + \
						0x3ac + (x))
#define MCHP_ESPI_MBOX_SIRQ0		0
#define MCHP_ESPI_MBOX_SIRQ1		1
#define MCHP_ESPI_8042_SIRQ0		2
#define MCHP_ESPI_8042_SIRQ1		3
#define MCHP_ESPI_ACPI_EC0_SIRQ		4
#define MCHP_ESPI_ACPI_EC1_SIRQ		5
#define MCHP_ESPI_ACPI_EC2_SIRQ		6
#define MCHP_ESPI_ACPI_EC3_SIRQ		7
#define MCHP_ESPI_ACPI_EC4_SIRQ		8
#define MCHP_ESPI_UART0_SIRQ		9
#define MCHP_ESPI_UART1_SIRQ		10
#define MCHP_ESPI_EMI0_SIRQ0		11
#define MCHP_ESPI_EMI0_SIRQ1		12
#define MCHP_ESPI_EMI1_SIRQ0		13
#define MCHP_ESPI_EMI1_SIRQ1		14
#define MCHP_ESPI_EMI2_SIRQ0		15
#define MCHP_ESPI_EMI2_SIRQ1		16
#define MCHP_ESPI_RTC_SIRQ		17
#define MCHP_ESPI_EC_SIRQ		18

/* eSPI Virtual Wire Error Register */
#define MCHP_ESPI_IO_VW_ERROR	REG8(MCHP_ESPI_IO_BASE + 0x3f0)


/* eSPI Memory Component Base Address */
#define MCHP_ESPI_MEM_BASE		0x400f3800

/*
 * eSPI Logical Device Memory Host BAR's to specify Host memory
 * base address and valid bit.
 * Each Logical Device implementing memory access has an 80-bit register.
 * b[0]=Valid
 * b[15:1]=0(reserved)
 * b[79:16]=eSPI bus memory address(Host address space)
 */
#define MCHP_ESPI_MBAR_MBOX_ID		0
#define MCHP_ESPI_MBAR_ACPI_EC0_ID	1
#define MCHP_ESPI_MBAR_ACPI_EC1_ID	2
#define MCHP_ESPI_MBAR_ACPI_EC2_ID	3
#define MCHP_ESPI_MBAR_ACPI_EC3_ID	4
#define MCHP_ESPI_MBAR_ACPI_EC4_ID	5
#define MCHP_ESPI_MBAR_EMI0_ID		6
#define MCHP_ESPI_MBAR_EMI1_ID		7
#define MCHP_ESPI_MBAR_EMI2_ID		8

#define MCHP_ESPI_MBAR_VALID(x) \
	REG8(MCHP_ESPI_MEM_BASE + 0x130 + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_MBAR_HOST_ADDR_0_15(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x132 + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_MBAR_HOST_ADDR_16_31(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x134 + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_MBAR_HOST_ADDR_32_47(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x136 + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_MBAR_HOST_ADDR_48_63(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x138 + ((x) << 3) + ((x) << 1))

/*
 * eSPI SRAM BAR's
 * b[0,3,8:15] = 0 reserved
 * b[2:1] = access
 * b[7:4] = size
 * b[79:16] = Host address
 */
#define MCHP_ESPI_SRAM_BAR_CFG(x) \
	REG8(MCHP_ESPI_MEM_BASE + 0x1ac + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_SRAM_BAR_ADDR_0_15(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x1ae + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_SRAM_BAR_ADDR_16_31(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x1b0 + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_SRAM_BAR_ADDR_32_47(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x1b2 + ((x) << 3) + ((x) << 1))

#define MCHP_ESPI_SRAM_BAR_ADDR_48_63(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x1b4 + ((x) << 3) + ((x) << 1))

/* eSPI Memory Bus Master Registers */
#define MCHP_ESPI_BM_STATUS \
	REG32(MCHP_ESPI_MEM_BASE + 0x200)
#define MCHP_ESPI_BM_IEN \
	REG32(MCHP_ESPI_MEM_BASE + 0x204)
#define MCHP_ESPI_BM_CONFIG \
	REG32(MCHP_ESPI_MEM_BASE + 0x208)
#define MCHP_ESPI_BM1_CTL \
	REG32(MCHP_ESPI_MEM_BASE + 0x210)
#define MCHP_ESPI_BM1_HOST_ADDR_LO \
		REG32(MCHP_ESPI_MEM_BASE + 0x214)
#define MCHP_ESPI_BM1_HOST_ADDR_HI \
	REG32(MCHP_ESPI_MEM_BASE + 0x218)
#define MCHP_ESPI_BM1_EC_ADDR \
	REG32(MCHP_ESPI_MEM_BASE + 0x21c)
#define MCHP_ESPI_BM2_CTL \
	REG32(MCHP_ESPI_MEM_BASE + 0x224)
#define MCHP_ESPI_BM2_HOST_ADDR_LO \
	REG32(MCHP_ESPI_MEM_BASE + 0x228)
#define MCHP_ESPI_BM2_HOST_ADDR_HI \
	REG32(MCHP_ESPI_MEM_BASE + 0x22c)
#define MCHP_ESPI_BM2_EC_ADDR \
	REG32(MCHP_ESPI_MEM_BASE + 0x230)

/*
 * eSPI Memory BAR's for Logical Devices
 * b[0] = Valid
 * b[2:1] = access
 * b[3] = 0 reserved
 * b[7:4] = size
 * b[15:8] = 0 reserved
 * b[47:16] = EC SRAM Address where Host address is mapped
 * b[79:48] = 0 reserved
 *
 * BAR's start at offset 0x330
 */
#define MCHP_ESPI_MBAR_EC_VSIZE(x) \
	REG32(MCHP_ESPI_MEM_BASE + 0x330 + ((x) << 3) + ((x) << 1))
#define MCHP_ESPI_MBAR_EC_ADDR_0_15(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x332 + ((x) << 3) + ((x) << 1))
#define MCHP_ESPI_MBAR_EC_ADDR_16_31(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x334 + ((x) << 3) + ((x) << 1))
#define MCHP_ESPI_MBAR_EC_ADDR_32_47(x) \
	REG16(MCHP_ESPI_MEM_BASE + 0x336 + ((x) << 3) + ((x) << 1))

/* eSPI Virtual Wire Component Base Address */
#define MCHP_ESPI_VW_BASE		0x400f9c00

#define MCHP_ESPI_MSVW_BASE	(MCHP_ESPI_VW_BASE)
#define MCHP_ESPI_SMVW_BASE	((MCHP_ESPI_VW_BASE) + 0x200ul)

#if defined(CHIP_FAMILY_MEC152X)
#define MCHP_ESPI_MSVW_LEN	11
#define MCHP_ESPI_SMVW_LEN	11
#else
#define MCHP_ESPI_MSVW_LEN	12
#define MCHP_ESPI_SMVW_LEN	8
#endif 

#define MCHP_ESPI_MSVW_ADDR(n) ((MCHP_ESPI_MSVW_BASE) + \
	((n) * (MCHP_ESPI_MSVW_LEN)))

#define MCHP_ESPI_MSVW_MTOS_BITPOS		4

#define MCHP_ESPI_MSVW_IRQSEL_LEVEL_LO		0
#define MCHP_ESPI_MSVW_IRQSEL_LEVEL_HI		1
#define MCHP_ESPI_MSVW_IRQSEL_DISABLED		4
#define MCHP_ESPI_MSVW_IRQSEL_RISING		0x0d
#define MCHP_ESPI_MSVW_IRQSEL_FALLING		0x0e
#define MCHP_ESPI_MSVW_IRQSEL_BOTH_EDGES	0x0f



/*
 * Mapping of eSPI Master Host VWire group indices to
 * MCHP eSPI Master to Slave 96-bit VWire registers.
 * MSVW_xy where xy = PCH VWire number.
 * Each PCH VWire number controls 4 virtual wires.
 */
#define MSVW_H02	0
#define MSVW_H03	1
#define MSVW_H07	2
#define MSVW_H41	3
#define MSVW_H42	4
#define MSVW_H43	5
#define MSVW_H44	6
#define MSVW_H47	7
#define MSVW_H4A	8
#define MSVW_HSPARE0	9
#define MSVW_HSPARE1	10
#define MSVW_MAX	11


/* Access 32-bit word in 96-bit MSVW register. 0 <= w <= 2 */
#define MSVW(id, w) REG32(MCHP_ESPI_MSVW_BASE + ((id) << 3) + \
	((id << 2)) + (((w) & 0x03) << 2))

/* Access index value in byte 0 */
#define MCHP_ESPI_VW_M2S_INDEX(id) \
	REG8(MCHP_ESPI_VW_BASE + ((id) << 3) + ((id) << 2))

/*
 * Access MTOS_SOURCE and MTOS_STATE in byte 1
 * MTOS_SOURCE = b[1:0] specifies reset source
 * MTOS_STATE = b[7:4] are states loaded into SRC[0:3] on reset event
 */
#define MCHP_ESPI_VW_M2S_MTOS(id) \
	REG8(MCHP_ESPI_VW_BASE + 1 + ((id) << 3) + ((id) << 2))

/*
 * Access Index, MTOS Source, and MTOS State as 16-bit quantity.
 * Index in b[7:0]
 * MTOS Source in b[9:8]
 * MTOS State in b[15:12]
 */
#define MCHP_ESPI_VW_M2S_INDEX_MTOS(id) \
	REG16(MCHP_ESPI_VW_BASE + ((id) << 3) + ((id) << 2))

/* Access SRCn IRQ Select bit fields */
#define MCHP_ESPI_VW_M2S_IRQSEL0(id) \
	(REG8(MCHP_ESPI_VW_BASE + 4 + ((id) << 3) + ((id) << 2)))

#define MCHP_ESPI_VW_M2S_IRQSEL1(id) \
	(REG8(MCHP_ESPI_VW_BASE + 5 + ((id) << 3) + ((id) << 2)))

#define MCHP_ESPI_VW_M2S_IRQSEL2(id) \
	(REG8(MCHP_ESPI_VW_BASE + 6 + ((id) << 3) + ((id) << 2)))

#define MCHP_ESPI_VW_M2S_IRQSEL3(id) \
	(REG8(MCHP_ESPI_VW_BASE + 7 + ((id) << 3) + ((id) << 2)))

#define MCHP_ESPI_VW_M2S_IRQSEL(id, src) \
	REG8(MCHP_ESPI_VW_BASE + 4 + ((id) << 3) + ((id) << 2) + \
						((src) & 0x03))

#define MCHP_ESPI_VW_M2S_IRQSEL_ALL(id) \
	(REG32(MCHP_ESPI_VW_BASE + 4 + ((id) << 3) + ((id) << 2)))

/* Access individual source bits */
#define MCHP_ESPI_VW_M2S_SRC0(id) \
	REG8(MCHP_ESPI_VW_BASE + 8 + ((id) << 3) + ((id) << 2))

#define MCHP_ESPI_VW_M2S_SRC1(id) \
	REG8(MCHP_ESPI_VW_BASE + 9 + ((id) << 3) + ((id) << 2))

#define MCHP_ESPI_VW_M2S_SRC2(id) \
	REG8(MCHP_ESPI_VW_BASE + 10 + ((id) << 3) + ((id) << 2))

#define MCHP_ESPI_VW_M2S_SRC3(id) \
	REG8(MCHP_ESPI_VW_BASE + 11 + ((id) << 3) + ((id) << 2))

/*
 * Access all four Source bits as 32-bit value, Source bits are located
 * at bits[0, 8, 16, 24] of 32-bit word.
 */
#define MCHP_ESPI_VW_M2S_SRC_ALL(id) \
	REG32(MCHP_ESPI_VW_BASE + 8 + ((id) << 3) + ((id) << 2))

/*
 * Access an individual Source bit as byte where
 * bit[0] contains the source bit.
 */
#define MCHP_ESPI_VW_M2S_SRC(id, src) \
	REG8(MCHP_ESPI_VW_BASE + 8 + ((id) << 3) + ((src) & 0x03))



/*
 * Indices of Slave to Master Virtual Wire registers.
 * Registers are 64-bit.
 * Host chipset groups VWires into groups of 4 with
 * a spec. defined index.
 * SMVW_Ixy where xy = eSPI Master defined index.
 * MCHP maps Host indices into its Slave to Master
 * 64-bit registers.
 */
#define SMVW_H04	0
#define SMVW_H05	1
#define SMVW_H06	2
#define SMVW_H40	3
#define SMVW_H45	4
#define SMVW_H46	5
#define SMVW_HSPARE6	6
#define SMVW_HSPARE7	7
#define SMVW_HSPARE8	8
#define SMVW_HSPARE9	9
#define SMVW_HSPARE10	10
#define SMVW_MAX	11


/* Access 32-bit word of 64-bit SMVW register, 0 <= w <= 1 */
#define SMVW(id, w) REG32(MCHP_ESPI_VW_BASE + 0x200 + ((id) << 3) + \
		(((w) & 0x01) << 2))

/* Access Index in b[7:0] of byte 0 */
#define MCHP_ESPI_VW_S2M_INDEX(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x200 + ((id) << 3))

/* Access STOM_SOURCE and STOM_STATE in byte 1
 * STOM_SOURCE = b[1:0]
 * STOM_STATE = b[7:4]
 */
#define MCHP_ESPI_VW_S2M_STOM(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x201 + ((id) << 3))

/* Access Index, STOM_SOURCE, and STOM_STATE in bytes[1:0]
 * Index = b[7:0]
 * STOM_SOURCE = b[9:8]
 * STOM_STATE = [15:12]
 */
#define MCHP_ESPI_VW_S2M_INDEX_STOM(id) \
	REG16(MCHP_ESPI_VW_BASE + 0x200 + ((id) << 3))

/* Access Change[0:3] RO bits. Set to 1 if any of SRC[0:3] change */
#define MCHP_ESPI_VW_S2M_CHANGE(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x202 + ((id) << 3))

/* Access individual SRC bits
 * bit[0] = SRCn
 */
#define MCHP_ESPI_VW_S2M_SRC0(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x204 + ((id) << 3))

#define MCHP_ESPI_VW_S2M_SRC1(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x205 + ((id) << 3))

#define MCHP_ESPI_VW_S2M_SRC2(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x206 + ((id) << 3))

#define MCHP_ESPI_VW_S2M_SRC3(id) \
	REG8(MCHP_ESPI_VW_BASE + 0x206 + ((id) << 3))

/*
 * Access specified source bit as byte read/write.
 * Source bit is in bit[0] of byte.
 */
#define MCHP_ESPI_VW_S2M_SRC(id, src) \
	REG8(MCHP_ESPI_VW_BASE + 0x204 + ((id) << 3) + ((src) & 0x03))


/* Access SRC[0:3] as 32-bit word
 * SRC0 = b[0]
 * SRC1 = b[8]
 * SRC2 = b[16]
 * SRC3 = b[24]
 */
#define MCHP_ESPI_VW_S2M_SRC_ALL(id) \
	REG32(MCHP_ESPI_VW_BASE + 0x204 + ((id) << 3))


/*
 * eSPI RESET, channel enables and operations except Master-to-Slave
 * WWires are all on GIRQ19
 */
#define MCHP_ESPI_GIRQ			19
#define MCHP_ESPI_PC_GIRQ_BIT		(1ul << 0)
#define MCHP_ESPI_BM1_GIRQ_BIT		(1ul << 1)
#define MCHP_ESPI_BM2_GIRQ_BIT		(1ul << 2)
#define MCHP_ESPI_LTR_GIRQ_BIT		(1ul << 3)
#define MCHP_ESPI_OOB_TX_GIRQ_BIT	(1ul << 4)
#define MCHP_ESPI_OOB_RX_GIRQ_BIT	(1ul << 5)
#define MCHP_ESPI_FC_GIRQ_BIT		(1ul << 6)
#define MCHP_ESPI_RESET_GIRQ_BIT	(1ul << 7)
#define MCHP_ESPI_VW_EN_GIRQ_BIT	(1ul << 8)

/*
 * eSPI Master-to-Slave WWire interrupts are on GIRQ24 and GIRQ25
 */
#define MCHP_ESPI_MSVW_0_6_GIRQ		24
#define MCHP_ESPI_MSVW_7_10_GIRQ	25
/*
 * Four source bits, SRC[0:3] per Master-to-Slave register
 * v = MSVW [0:10]
 * n = VWire SRC bit = [0:3]
 */
#define MCHP_ESPI_MSVW_GIRQ(v) (24 + ((v) > 6 ? 1 : 0))

#define MCHP_ESPI_MSVW_SRC_GIRQ_BIT(v, n) \
	(((v) > 6) ? (1ul << (((v)-7)+(n))) : (1ul << ((v)+(n))))

#if defined(CHIP_FAMILY_MEC152X)
/* PECI register */
#define MCHP_PECI_BASE		0x40006400
#define MCHP_PECI_WRITE_DATA			REG8(MCHP_PECI_BASE)
#define MCHP_PECI_READ_DATA				REG8(MCHP_PECI_BASE + 0x04)
#define MCHP_PECI_CONTROL				REG8(MCHP_PECI_BASE + 0x08)
#define MCHP_PECI_STATUS1				REG8(MCHP_PECI_BASE + 0x0C)
#define MCHP_PECI_STATUS2				REG8(MCHP_PECI_BASE + 0x10)
#define MCHP_PECI_ERROR					REG8(MCHP_PECI_BASE + 0x14)
#define MCHP_PECI_INT_ENABLE1			REG8(MCHP_PECI_BASE + 0x18)
#define MCHP_PECI_INT_ENABLE2			REG8(MCHP_PECI_BASE + 0x1C)
#define MCHP_PECI_OPTIMAL_BIT_TIME_L	REG8(MCHP_PECI_BASE + 0x20)
#define MCHP_PECI_OPTIMAL_BIT_TIME_H	REG8(MCHP_PECI_BASE + 0x24)
#define MCHP_PECI_BAUD_CTRL				REG32(MCHP_PECI_BASE + 0x30)
#define MCHP_PECI_BLOCK_ID				REG32(MCHP_PECI_BASE + 0x40)
#define MCHP_PECI_REVISION				REG32(MCHP_PECI_BASE + 0x44)

/* PECI register bit definitions */
#define MCHP_PECI_STATUS1_BOF		(1<<0)
#define MCHP_PECI_STATUS1_EOF		(1<<1)
#define MCHP_PECI_STATUS1_ERR		(1<<2)
#define MCHP_PECI_STATUS1_RDY		(1<<3)
#define MCHP_PECI_STATUS1_RDYLO		(1<<4)
#define MCHP_PECI_STATUS1_RDYHI		(1<<5)
#define MCHP_PECI_STATUS1_MINT		(1<<7)

#define MCHP_PECI_STATUS2_WFF		(1<<0)
#define MCHP_PECI_STATUS2_WFE		(1<<1)
#define MCHP_PECI_STATUS2_RFF		(1<<2)
#define MCHP_PECI_STATUS2_RFE		(1<<3)
#define MCHP_PECI_STATUS2_IDLE		(1<<7)

#define MCHP_PECI_ERROR_FERR		(1<<0)
#define MCHP_PECI_ERROR_BERR		(1<<1)
#define MCHP_PECI_ERROR_REQERR		(1<<3)
#define MCHP_PECI_ERROR_WROV		(1<<4)
#define MCHP_PECI_ERROR_WRUN		(1<<5)
#define MCHP_PECI_ERROR_RDOV		(1<<6)
#define MCHP_PECI_ERROR_CLKERR		(1<<7)

#define MCHP_PECI_CONTROL_PD		(1<<0)
#define MCHP_PECI_CONTROL_RST		(1<<3)
#define MCHP_PECI_CONTROL_FRST		(1<<5)
#define MCHP_PECI_CONTROL_TXEN		(1<<6)
#define MCHP_PECI_CONTROL_MIEN		(1<<7)
#endif

/* DMA */
#define MCHP_DMA_BASE		0x40002400
#define MCHP_DMA_CH_OFS		0x40
#define MCHP_DMA_CH_OFS_BITPOS	6
#define MCHP_DMA_CH_BASE (MCHP_DMA_BASE + MCHP_DMA_CH_OFS)

#define MCHP_DMA_MAIN_CTRL	REG8(MCHP_DMA_BASE + 0x00)
#define MCHP_DMA_MAIN_PKT_RO	REG32(MCHP_DMA_BASE + 0x04)
#define MCHP_DMA_MAIN_FSM_RO	REG8(MCHP_DMA_BASE + 0x08)

/* DMA Channel Registers */
#define MCHP_DMA_CH_ACT(n) \
	REG8(MCHP_DMA_CH_BASE + ((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_MEM_START(n) \
	REG32(MCHP_DMA_CH_BASE + 0x04 + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_MEM_END(n) \
	REG32(MCHP_DMA_CH_BASE + 0x08 + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_DEV_ADDR(n) \
	REG32(MCHP_DMA_CH_BASE + 0x0c + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_CTRL(n) \
	REG32(MCHP_DMA_CH_BASE + 0x10 + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_ISTS(n) \
	REG8(MCHP_DMA_CH_BASE + 0x14 + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_IEN(n) \
	REG8(MCHP_DMA_CH_BASE + 0x18 + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

#define MCHP_DMA_CH_FSM_RO(n) \
	REG16(MCHP_DMA_CH_BASE + 0x1c + \
			((n) << MCHP_DMA_CH_OFS_BITPOS))

/*
 * DMA Channel 0 implements CRC-32 feature
 */
#define MCHP_DMA_CH0_CRC32_EN		REG8(MCHP_DMA_CH_BASE + 0x20)
#define MCHP_DMA_CH0_CRC32_DATA		REG32(MCHP_DMA_CH_BASE + 0x24)
#define MCHP_DMA_CH0_CRC32_POST_STS	REG8(MCHP_DMA_CH_BASE + 0x28)

/*
 * DMA Channel 1 implements memory fill feature
 */
#define MCHP_DMA_CH1_FILL_EN \
	REG8(MCHP_DMA_CH_BASE + MCHP_DMA_CH_OFS + 0x20)
#define MCHP_DMA_CH1_FILL_DATA \
	REG32(MCHP_DMA_CH_BASE + MCHP_DMA_CH_OFS + 0x24)


/*
 * Available DMA channels.
 *
 * On MCHP, any DMA channel may serve any device. Since we have
 * 14 channels and 14 devices, we make each channel dedicated to the
 * device of the same number.
 */
#if defined(CHIP_FAMILY_MEC152X)
enum dma_channel {
	/* Channel numbers */
	MCHP_DMAC_I2C0_SLAVE =  0,
	MCHP_DMAC_I2C0_MASTER = 1,

	MCHP_DMAC_I2C1_SLAVE =  2,
	MCHP_DMAC_I2C1_MASTER = 3,

	MCHP_DMAC_I2C2_SLAVE =  4,
	MCHP_DMAC_I2C2_MASTER = 5,
/*
	MCHP_DMAC_I2C3_SLAVE =  6,
	MCHP_DMAC_I2C3_MASTER = 7,
*/
	MCHP_DMAC_SPI0_TX =     6,
	MCHP_DMAC_SPI0_RX =     7,
	MCHP_DMAC_SPI1_TX =    8,
	MCHP_DMAC_SPI1_RX =    9,
	MCHP_DMAC_QMSPI0_TX =  10,
	MCHP_DMAC_QMSPI0_RX =  11,
	/* Channel count */
	MCHP_DMAC_COUNT =      12,
};

/*
 * Peripheral device DMA Device ID's for bits [15:9]
 * in DMA channel control register.
 */
#define MCHP_DMA_I2C0_SLV_REQ_ID    0
#define MCHP_DMA_I2C0_MTR_REQ_ID    1
#define MCHP_DMA_I2C1_SLV_REQ_ID    2
#define MCHP_DMA_I2C1_MTR_REQ_ID    3
#define MCHP_DMA_I2C2_SLV_REQ_ID    4
#define MCHP_DMA_I2C2_MTR_REQ_ID    5
#define MCHP_DMA_SPI0_TX_REQ_ID     6
#define MCHP_DMA_SPI0_RX_REQ_ID     7
#define MCHP_DMA_SPI1_TX_REQ_ID     8
#define MCHP_DMA_SPI1_RX_REQ_ID     9
#define MCHP_DMA_QMSPI0_TX_REQ_ID   10
#define MCHP_DMA_QMSPI0_RX_REQ_ID   11

#else
enum dma_channel {
	/* Channel numbers */
	MCHP_DMAC_I2C0_SLAVE =  0,
	MCHP_DMAC_I2C0_MASTER = 1,
	MCHP_DMAC_I2C1_SLAVE =  2,
	MCHP_DMAC_I2C1_MASTER = 3,
	MCHP_DMAC_I2C2_SLAVE =  4,
	MCHP_DMAC_I2C2_MASTER = 5,
	MCHP_DMAC_I2C3_SLAVE =  6,
	MCHP_DMAC_I2C3_MASTER = 7,
	MCHP_DMAC_SPI0_TX =     8,
	MCHP_DMAC_SPI0_RX =     9,
	MCHP_DMAC_SPI1_TX =    10,
	MCHP_DMAC_SPI1_RX =    11,
	MCHP_DMAC_QMSPI0_TX =  12,
	MCHP_DMAC_QMSPI0_RX =  13,
	/* Channel count */
	MCHP_DMAC_COUNT =      14,
};

/*
 * Peripheral device DMA Device ID's for bits [15:9]
 * in DMA channel control register.
 */
#define MCHP_DMA_I2C0_SLV_REQ_ID	0
#define MCHP_DMA_I2C0_MTR_REQ_ID	1
#define MCHP_DMA_I2C1_SLV_REQ_ID	2
#define MCHP_DMA_I2C1_MTR_REQ_ID	3
#define MCHP_DMA_I2C2_SLV_REQ_ID	4
#define MCHP_DMA_I2C2_MTR_REQ_ID	5
#define MCHP_DMA_I2C3_SLV_REQ_ID	6
#define MCHP_DMA_I2C3_MTR_REQ_ID	7
#define MCHP_DMA_GPSPI0_TX_REQ_ID	8
#define MCHP_DMA_GPSPI0_RX_REQ_ID	9
#define MCHP_DMA_GPSPI1_TX_REQ_ID	10
#define MCHP_DMA_GPSPI1_RX_REQ_ID	11
#define MCHP_DMA_QMSPI0_TX_REQ_ID	12
#define MCHP_DMA_QMSPI0_RX_REQ_ID	13
#endif



/* Bits for DMA Main Control */
#define MCHP_DMA_MAIN_CTRL_ACT	BIT(0)
#define MCHP_DMA_MAIN_CTRL_SRST	BIT(1)

/* Bits for DMA channel regs */
#define MCHP_DMA_ACT_EN		BIT(0)
/* DMA Channel Control */
#define MCHP_DMA_ABORT		BIT(25)
#define MCHP_DMA_SW_GO		BIT(24)
#define MCHP_DMA_XFER_SIZE_MASK	(7ul << 20)
#define MCHP_DMA_XFER_SIZE(x)	((x) << 20)
#define MCHP_DMA_DIS_HW_FLOW	BIT(19)
#define MCHP_DMA_INC_DEV	BIT(17)
#define MCHP_DMA_INC_MEM	BIT(16)
#define MCHP_DMA_DEV(x)		((x) << 9)
#define MCHP_DMA_DEV_MASK0	(0x7f)
#define MCHP_DMA_DEV_MASK	(0x7f << 9)
#define MCHP_DMA_TO_DEV		BIT(8)
#define MCHP_DMA_DONE		BIT(2)
#define MCHP_DMA_RUN		BIT(0)
/* DMA Channel Status */
#define MCHP_DMA_STS_ALU_DONE	BIT(3)
#define MCHP_DMA_STS_DONE	BIT(2)
#define MCHP_DMA_STS_HWFL_ERR	BIT(1)
#define MCHP_DMA_STS_BUS_ERR	BIT(0)

/*
 * Required structure typedef for common/dma.h interface
 * !!! checkpatch.pl will not like this !!!
 */

/* Registers for a single channel of the DMA controller */
struct MCHP_dma_chan {
	uint32_t act;		/* Activate */
	uint32_t mem_start;	/* Memory start address */
	uint32_t mem_end;	/* Memory end address */
	uint32_t dev;		/* Device address */
	uint32_t ctrl;		/* Control */
	uint32_t int_status;	/* Interrupt status */
	uint32_t int_enabled;	/* Interrupt enabled */
	uint32_t chfsm;		/* channel fsm read-only */
	uint32_t alu_en;	/* channels 0 & 1 only */
	uint32_t alu_data;	/* channels 0 & 1 only */
	uint32_t alu_sts;	/* channel 0 only */
	uint32_t alu_ro;	/* channel 0 only */
	uint32_t rsvd[4];	/* 0x30 - 0x3F */
};

/* Common code and header file must use this */
typedef struct MCHP_dma_chan dma_chan_t;

/*
 * Hardware delay register.
 * Write of 0 <= n <= 31 will stall the Cortex-M4
 * for n+1 microseconds. Interrupts will not be
 * serviced during the delay period. Reads have
 * no effect.
 */
#define MCHP_USEC_DELAY_REG_ADDR	(0x10000000)
#define MCHP_USEC_DELAY(x)	(REG8(MCHP_USEC_DELAY_REG_ADDR) = (x))

/* IRQ Numbers */
#ifdef CHIP_FAMILY

#if defined(CHIP_FAMILY_MEC17XX) || defined(CHIP_FAMILY_MEC152X)

#define MCHP_IRQ_GIRQ8		0
#define MCHP_IRQ_GIRQ9		1
#define MCHP_IRQ_GIRQ10		2
#define MCHP_IRQ_GIRQ11		3
#define MCHP_IRQ_GIRQ12		4
#define MCHP_IRQ_GIRQ13		5
#define MCHP_IRQ_GIRQ14		6
#define MCHP_IRQ_GIRQ15		7
#define MCHP_IRQ_GIRQ16		8
#define MCHP_IRQ_GIRQ17		9
#define MCHP_IRQ_GIRQ18		10
#define MCHP_IRQ_GIRQ19		11
#define MCHP_IRQ_GIRQ20		12
#define MCHP_IRQ_GIRQ21		13
/*
 * GIRQ22 is not connected to NVIC, it wakes peripheral
 * subsystem but not ARM core.
 */
#define MCHP_IRQ_GIRQ23		14
#define MCHP_IRQ_GIRQ24		15
#define MCHP_IRQ_GIRQ25		16
#define MCHP_IRQ_GIRQ26		17
/* 18 - 19 Not connected */
/* The following I2C definitions are for SMBUS */ 
#define MCHP_IRQ_I2C_0		20 
#define MCHP_IRQ_I2C_1		21
#define MCHP_IRQ_I2C_2		22
#define MCHP_IRQ_I2C_3		23
#define MCHP_IRQ_DMA_0		24
#define MCHP_IRQ_DMA_1		25
#define MCHP_IRQ_DMA_2		26
#define MCHP_IRQ_DMA_3		27
#define MCHP_IRQ_DMA_4		28
#define MCHP_IRQ_DMA_5		29
#define MCHP_IRQ_DMA_6		30
#define MCHP_IRQ_DMA_7		31
#define MCHP_IRQ_DMA_8		32
#define MCHP_IRQ_DMA_9		33
#define MCHP_IRQ_DMA_10		34
#define MCHP_IRQ_DMA_11		35

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_DMA_12		36
#define MCHP_IRQ_DMA_13		37
#endif 

/* 38 - 39 Not connected */
#define MCHP_IRQ_UART0		40
#define MCHP_IRQ_UART1		41
#define MCHP_IRQ_EMI0		42
#define MCHP_IRQ_EMI1		43
#define MCHP_IRQ_EMI2		44
#define MCHP_IRQ_ACPIEC0_IBF	45
#define MCHP_IRQ_ACPIEC0_OBE	46
#define MCHP_IRQ_ACPIEC1_IBF	47
#define MCHP_IRQ_ACPIEC1_OBE	48
#define MCHP_IRQ_ACPIEC2_IBF	49
#define MCHP_IRQ_ACPIEC2_OBE	50
#define MCHP_IRQ_ACPIEC3_IBF	51
#define MCHP_IRQ_ACPIEC3_OBE	52

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_ACPIEC4_IBF	53
#define MCHP_IRQ_ACPIEC4_OBE	54
#endif 

#define MCHP_IRQ_ACPIPM1_CTL	55
#define MCHP_IRQ_ACPIPM1_EN	56
#define MCHP_IRQ_ACPIPM1_STS	57
#define MCHP_IRQ_8042EM_OBE	58
#define MCHP_IRQ_8042EM_IBF	59
#define MCHP_IRQ_MAILBOX_DATA	60
/* 61 Not connected */
#define MCHP_IRQ_PORT80DBG0	62
#define MCHP_IRQ_PORT80DBG1	63
/* 64 Not connected */
#define MCHP_IRQ_PKE_ERR	65
#define MCHP_IRQ_PKE_END	66
#define MCHP_IRQ_NDRNG		67
#define MCHP_IRQ_AES		68
#define MCHP_IRQ_HASH		69
#define MCHP_IRQ_PECI_HOST	70
#define MCHP_IRQ_TACH_0		71
#define MCHP_IRQ_TACH_1		72
#define MCHP_IRQ_TACH_2		73

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_FAN0_FAIL	74
#define MCHP_IRQ_FAN0_STALL	75
#define MCHP_IRQ_FAN1_FAIL	76
#define MCHP_IRQ_FAN1_STALL	77
#endif 

#define MCHP_IRQ_ADC_SNGL	78
#define MCHP_IRQ_ADC_RPT	79

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_RCID0		80
#define MCHP_IRQ_RCID1		81
#define MCHP_IRQ_RCID2		82
#endif 

#define MCHP_IRQ_LED0_WDT	83
#define MCHP_IRQ_LED1_WDT	84
#define MCHP_IRQ_LED2_WDT	85

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_LED3_WDT	86
#endif 

#define MCHP_IRQ_PHOT		87

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_PWRGRD0	88
#define MCHP_IRQ_PWRGRD1	89
#endif 

#define MCHP_IRQ_LPC		90
#define MCHP_IRQ_QMSPI0		91

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_SPI0_TX	92
#define MCHP_IRQ_SPI0_RX	93
#define MCHP_IRQ_SPI1_TX	94
#define MCHP_IRQ_SPI1_RX	95
#define MCHP_IRQ_BCM0_ERR	96
#define MCHP_IRQ_BCM0_BUSY	97
#define MCHP_IRQ_BCM1_ERR	98
#define MCHP_IRQ_BCM1_BUSY	99
#endif 

#define MCHP_IRQ_PS2_0		100
#define MCHP_IRQ_PS2_1		101

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_PS2_2		102
#endif 

#define MCHP_IRQ_ESPI_PC	103
#define MCHP_IRQ_ESPI_BM1	104
#define MCHP_IRQ_ESPI_BM2	105
#define MCHP_IRQ_ESPI_LTR	106
#define MCHP_IRQ_ESPI_OOB_UP	107
#define MCHP_IRQ_ESPI_OOB_DN	108
#define MCHP_IRQ_ESPI_FC	109
#define MCHP_IRQ_ESPI_RESET	110
#define MCHP_IRQ_RTOS_TIMER	111
#define MCHP_IRQ_HTIMER0	112
#define MCHP_IRQ_HTIMER1	113
#define MCHP_IRQ_WEEK_ALARM	114
#define MCHP_IRQ_SUBWEEK	115
#define MCHP_IRQ_WEEK_SEC	116
#define MCHP_IRQ_WEEK_SUBSEC	117
#define MCHP_IRQ_WEEK_SYSPWR	118
#define MCHP_IRQ_RTC		119
#define MCHP_IRQ_RTC_ALARM	120
#define MCHP_IRQ_VCI_OVRD_IN	121
#define MCHP_IRQ_VCI_IN0	122
#define MCHP_IRQ_VCI_IN1	123
#define MCHP_IRQ_VCI_IN2	124
#define MCHP_IRQ_VCI_IN3	125

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_VCI_IN4	126
#define MCHP_IRQ_VCI_IN5	127
#define MCHP_IRQ_VCI_IN6	128
#endif 

#define MCHP_IRQ_PS20A_WAKE	129
#define MCHP_IRQ_PS20B_WAKE	130

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_PS21A_WAKE	131
#endif 

#define MCHP_IRQ_PS21B_WAKE	132

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_PS2_2_WAKE	133
#define MCHP_IRQ_ENVMON		134
#endif 

#define MCHP_IRQ_KSC_INT	135
#define MCHP_IRQ_TIMER16_0	136
#define MCHP_IRQ_TIMER16_1	137

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_TIMER16_2	138
#define MCHP_IRQ_TIMER16_3	139
#endif 

#define MCHP_IRQ_TIMER32_0	140
#define MCHP_IRQ_TIMER32_1	141

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_CNTR_TM0	142
#define MCHP_IRQ_CNTR_TM1	143
#define MCHP_IRQ_CNTR_TM2	144
#define MCHP_IRQ_CNTR_TM3	145
#endif 

#define MCHP_IRQ_CCT_TMR	146
#define MCHP_IRQ_CCT_CAP0	147
#define MCHP_IRQ_CCT_CAP1	148
#define MCHP_IRQ_CCT_CAP2	149
#define MCHP_IRQ_CCT_CAP3	150
#define MCHP_IRQ_CCT_CAP4	151
#define MCHP_IRQ_CCT_CAP5	152
#define MCHP_IRQ_CCT_CMP0	153
#define MCHP_IRQ_CCT_CMP1	154
#define MCHP_IRQ_EEPROM		155
#define MCHP_IRQ_ESPI_VW_EN	156

#ifdef CHIP_FAMILY_MEC17XX
#define MCHP_IRQ_MAX		157
#endif /* CHIP_FAMILY_MEC17XX */

#ifdef CHIP_FAMILY_MEC152X
/* I2C_4 is SMBUS */
#define MCHP_IRQ_I2C_4		158
#define MCHP_IRQ_TACH_3		159
#define MCHP_IRQ_CEC_0		160
#define MCHP_IRQ_SAF_DONE	166
#define MCHP_IRQ_SAF_ERROR	167
#define MCHP_IRQ_I2CONLY_0		168
#define MCHP_IRQ_I2CONLY_1		169
#define MCHP_IRQ_I2CONLY_2		170
#define MCHP_IRQ_WDT		171
#define MCHP_IRQ_MAX		172

#endif /* CHIP_FAMILY_MEC152X */

#else
#error "BUILD ERROR: CHIP_FAMILY_MEC17XX or CHIP_FAMILY_MEC152X not defined!"
#endif /* #ifdef CHIP_FAMILY_MEC17XX CHIP_FAMILY_MEC152X */

#else
#error "BUILD ERROR: CHIP_FAMILY not defined!"
#endif /* #ifdef CHIP_FAMILY */

/* Wake pin definitions, defined at board-level */
#ifndef CONFIG_HIBERNATE_WAKE_PINS_DYNAMIC
extern const enum gpio_signal hibernate_wake_pins[];
extern const int hibernate_wake_pins_used;
#else
extern enum gpio_signal hibernate_wake_pins[];
extern int hibernate_wake_pins_used;
#endif


#endif /* __CROS_EC_REGISTERS_H */
