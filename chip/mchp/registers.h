/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for Microchip MEC family processors
 */
#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "compile_time_macros.h"

#if defined(CHIP_FAMILY_MEC152X)
#include "registers-mec152x.h"
#elif defined(CHIP_FAMILY_MEC170X)
#include "registers-mec1701.h"
#elif defined(CHIP_FAMILY_MEC172X)
#include "registers-mec172x.h"
#else
#error "Unsupported chip family"
#endif

/* Common registers */
/* EC Interrupt aggregator (ECIA) */
#define MCHP_INT_SOURCE(x) REG32(MCHP_INTx_BASE(x) + 0x0)
#define MCHP_INT_ENABLE(x) REG32(MCHP_INTx_BASE(x) + 0x4)
#define MCHP_INT_RESULT(x) REG32(MCHP_INTx_BASE(x) + 0x8)
#define MCHP_INT_DISABLE(x) REG32(MCHP_INTx_BASE(x) + 0xc)
#define MCHP_INT_BLK_EN REG32(MCHP_INT_BASE + 0x200)
#define MCHP_INT_BLK_DIS REG32(MCHP_INT_BASE + 0x204)
#define MCHP_INT_BLK_IRQ REG32(MCHP_INT_BASE + 0x208)

/* EC Chip Configuration */
#define MCHP_CHIP_LEGACY_DEV_ID REG8(MCHP_CHIP_BASE + 0x20)
#define MCHP_CHIP_LEGACY_DEV_REV REG8(MCHP_CHIP_BASE + 0x21)

/* Power/Clocks/Resets */
#define MCHP_PCR_SYS_SLP_CTL REG32(MCHP_PCR_BASE + 0x00)
#define MCHP_PCR_PROC_CLK_CTL REG32(MCHP_PCR_BASE + 0x04)
#define MCHP_PCR_SLOW_CLK_CTL REG32(MCHP_PCR_BASE + 0x08)
#define MCHP_PCR_CHIP_OSC_ID REG32(MCHP_PCR_BASE + 0x0C)
#define MCHP_PCR_PWR_RST_STS REG32(MCHP_PCR_BASE + 0x10)
#define MCHP_PCR_PWR_RST_CTL REG32(MCHP_PCR_BASE + 0x14)
#define MCHP_PCR_SYS_RST REG32(MCHP_PCR_BASE + 0x18)
#define MCHP_PCR_SLP_EN0 REG32(MCHP_PCR_BASE + 0x30)
#define MCHP_PCR_SLP_EN1 REG32(MCHP_PCR_BASE + 0x34)
#define MCHP_PCR_SLP_EN2 REG32(MCHP_PCR_BASE + 0x38)
#define MCHP_PCR_SLP_EN3 REG32(MCHP_PCR_BASE + 0x3C)
#define MCHP_PCR_SLP_EN4 REG32(MCHP_PCR_BASE + 0x40)
#define MCHP_PCR_CLK_REQ0 REG32(MCHP_PCR_BASE + 0x50)
#define MCHP_PCR_CLK_REQ1 REG32(MCHP_PCR_BASE + 0x54)
#define MCHP_PCR_CLK_REQ2 REG32(MCHP_PCR_BASE + 0x58)
#define MCHP_PCR_CLK_REQ3 REG32(MCHP_PCR_BASE + 0x5C)
#define MCHP_PCR_CLK_REQ4 REG32(MCHP_PCR_BASE + 0x60)
#define MCHP_PCR_RST_EN0 REG32(MCHP_PCR_BASE + 0x70)
#define MCHP_PCR_RST_EN1 REG32(MCHP_PCR_BASE + 0x74)
#define MCHP_PCR_RST_EN2 REG32(MCHP_PCR_BASE + 0x78)
#define MCHP_PCR_RST_EN3 REG32(MCHP_PCR_BASE + 0x7C)
#define MCHP_PCR_RST_EN4 REG32(MCHP_PCR_BASE + 0x80)
#define MCHP_PCR_SLP_EN(x) REG32(MCHP_PCR_BASE + 0x30 + ((x) << 2))
#define MCHP_PCR_CLK_REQ(x) REG32(MCHP_PCR_BASE + 0x50 + ((x) << 2))
#define MCHP_PCR_RST_EN(x) REG32(MCHP_PCR_BASE + 0x70 + ((x) << 2))

/* Bit definitions for MCHP_PCR_SYS_SLP_CTL */
#define MCHP_PCR_SYS_SLP_LIGHT (0ul << 0)
#define MCHP_PCR_SYS_SLP_HEAVY (1ul << 0)
#define MCHP_PCR_SYS_SLP_ALL (1ul << 3)
/*
 * Set/clear PCR sleep enable bit for single device
 * d bits[10:8] = register 0 - 4
 * d bits[4:0] = register bit position
 */
#define MCHP_PCR_SLP_EN_DEV(d) \
	(MCHP_PCR_SLP_EN(((d) >> 8) & 0x07) |= (1ul << ((d) & 0x1f)))
#define MCHP_PCR_SLP_DIS_DEV(d) \
	(MCHP_PCR_SLP_EN(((d) >> 8) & 0x07) &= ~(1ul << ((d) & 0x1f)))
/*
 * Set/clear bit pattern specified by mask in a single PCR sleep enable
 * register.
 * id = zero based ID of sleep enable register (0-4)
 * m = bit mask of bits to change
 */
#define MCHP_PCR_SLP_EN_DEV_MASK(id, m) (MCHP_PCR_SLP_EN((id)) |= (m))
#define MCHP_PCR_SLP_DIS_DEV_MASK(id, m) (MCHP_PCR_SLP_EN((id)) &= ~(m))
/* Slow Clock Control Mask */
#define MCHP_PCR_SLOW_CLK_CTL_MASK 0x03FFul

/* TFDP */
#define MCHP_TFDP_DATA REG8(MCHP_TFDP_BASE + 0x00)
#define MCHP_TFDP_CTRL REG8(MCHP_TFDP_BASE + 0x04)

/* UART */
#define MCHP_UART_ACT(x) REG8(MCHP_UART_CONFIG_BASE(x) + 0x30)
#define MCHP_UART_CFG(x) REG8(MCHP_UART_CONFIG_BASE(x) + 0xf0)
/* DLAB=0 */
#define MCHP_UART_RB(x) /*R*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x0)
#define MCHP_UART_TB(x) /*W*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x0)
#define MCHP_UART_IER(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x1)
/* DLAB=1 */
#define MCHP_UART_PBRG0(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x0)
#define MCHP_UART_PBRG1(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x1)
#define MCHP_UART_FCR(x) /*W*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x2)
#define MCHP_UART_IIR(x) /*R*/ REG8(MCHP_UART_RUNTIME_BASE(x) + 0x2)
#define MCHP_UART_LCR(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x3)
#define MCHP_UART_MCR(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x4)
#define MCHP_UART_LSR(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x5)
#define MCHP_UART_MSR(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x6)
#define MCHP_UART_SCR(x) REG8(MCHP_UART_RUNTIME_BASE(x) + 0x7)
/* Bit defines for MCHP_UARTx_LSR */
#define MCHP_LSR_TX_EMPTY BIT(5)

/* Timer */
#define MCHP_TMR16_CNT(x) REG32(MCHP_TMR16_BASE(x) + 0x0)
#define MCHP_TMR16_PRE(x) REG32(MCHP_TMR16_BASE(x) + 0x4)
#define MCHP_TMR16_STS(x) REG32(MCHP_TMR16_BASE(x) + 0x8)
#define MCHP_TMR16_IEN(x) REG32(MCHP_TMR16_BASE(x) + 0xc)
#define MCHP_TMR16_CTL(x) REG32(MCHP_TMR16_BASE(x) + 0x10)
#define MCHP_TMR32_CNT(x) REG32(MCHP_TMR32_BASE(x) + 0x0)
#define MCHP_TMR32_PRE(x) REG32(MCHP_TMR32_BASE(x) + 0x4)
#define MCHP_TMR32_STS(x) REG32(MCHP_TMR32_BASE(x) + 0x8)
#define MCHP_TMR32_IEN(x) REG32(MCHP_TMR32_BASE(x) + 0xc)
#define MCHP_TMR32_CTL(x) REG32(MCHP_TMR32_BASE(x) + 0x10)

/* RTimer */
#define MCHP_RTMR_COUNTER REG32(MCHP_RTMR_BASE + 0x00)
#define MCHP_RTMR_PRELOAD REG32(MCHP_RTMR_BASE + 0x04)
#define MCHP_RTMR_CONTROL REG8(MCHP_RTMR_BASE + 0x08)
#define MCHP_RTMR_SOFT_INTR REG8(MCHP_RTMR_BASE + 0x0c)

/* Watch dog timer */
#define MCHP_WDG_LOAD REG16(MCHP_WDG_BASE + 0x0)
#define MCHP_WDG_CTL REG16(MCHP_WDG_BASE + 0x4)
#define MCHP_WDG_KICK REG8(MCHP_WDG_BASE + 0x8)
#define MCHP_WDG_CNT REG16(MCHP_WDG_BASE + 0xc)
#define MCHP_WDT_CTL_ENABLE BIT(0)
#define MCHP_WDT_CTL_HTMR_STALL_EN BIT(2)
#define MCHP_WDT_CTL_WKTMR_STALL_EN BIT(3)
#define MCHP_WDT_CTL_JTAG_STALL_EN BIT(4)

/* Blinking-Breathing LED */
#define MCHP_BBLED_CONFIG(x) REG32(MCHP_BBLED_BASE(x) + 0x00)
#define MCHP_BBLED_LIMITS(x) REG32(MCHP_BBLED_BASE(x) + 0x04)
#define MCHP_BBLED_LIMIT_MIN(x) REG8(MCHP_BBLED_BASE(x) + 0x04)
#define MCHP_BBLED_LIMIT_MAX(x) REG8(MCHP_BBLED_BASE(x) + 0x06)
#define MCHP_BBLED_DELAY(x) REG32(MCHP_BBLED_BASE(x) + 0x08)
#define MCHP_BBLED_UPDATE_STEP(x) REG32(MCHP_BBLED_BASE(x) + 0x0C)
#define MCHP_BBLED_UPDATE_INTV(x) REG32(MCHP_BBLED_BASE(x) + 0x10)
#define MCHP_BBLED_OUTPUT_DLY(x) REG8(MCHP_BBLED_BASE(x) + 0x14)
/* BBLED Configuration Register */
#define MCHP_BBLED_ASYMMETRIC BIT(16)
#define MCHP_BBLED_WDT_RELOAD_BITPOS 8
#define MCHP_BBLED_WDT_RELOAD_MASK0 0xFFul
#define MCHP_BBLED_WDT_RELOAD_MASK (0xFFul << 8)
#define MCHP_BBLED_RESET BIT(7)
#define MCHP_BBLED_EN_UPDATE BIT(6)
#define MCHP_BBLED_PWM_SIZE_BITPOS 4
#define MCHP_BBLED_PWM_SIZE_MASK0 0x03ul
#define MCHP_BBLED_PWM_SIZE_MASK (0x03ul << 4)
#define MCHP_BBLED_PWM_SIZE_6BIT (0x02ul << 4)
#define MCHP_BBLED_PWM_SIZE_7BIT (0x01ul << 4)
#define MCHP_BBLED_PWM_SIZE_8BIT (0x00ul << 4)
#define MCHP_BBLED_SYNC BIT(3)
#define MCHP_BBLED_CLK_48M BIT(2)
#define MCHP_BBLED_CLK_32K 0
#define MCHP_BBLED_CTRL_MASK 0x03ul
#define MCHP_BBLED_CTRL_ALWAYS_ON 0x03ul
#define MCHP_BBLED_CTRL_BLINK 0x02ul
#define MCHP_BBLED_CTRL_BREATHE 0x01ul
#define MCHP_BBLED_CTRL_OFF 0x00ul
/* BBLED Delay Register */
#define MCHP_BBLED_DLY_MASK 0x0FFFul
#define MCHP_BBLED_DLY_LO_BITPOS 0
#define MCHP_BBLED_DLY_LO_MASK 0x0FFFul
#define MCHP_BBLED_DLY_HI_BITPOS 12
#define MCHP_BBLED_DLY_HI_MASK (0x0FFFul << 12)
/*
 * BBLED Update Step Register
 * 8 update fields numbered 0 - 7
 */
#define MCHP_BBLED_UPD_STEP_MASK0 0x0Ful
#define MCHP_BBLED_UPD_STEP_MASK(u) (0x0Ful << (((u) & 0x07) + 4))
/*
 * BBLED Update Interval Register
 * 8 interval fields numbered 0 - 7
 */
#define MCHP_BBLED_UPD_INTV_MASK0 0x0Ful
#define MCHP_BBLED_UPD_INTV_MASK(i) (0x0Ful << (((i) & 0x07) + 4))

/* EMI */
#define MCHP_EMI_H2E_MBX(n) REG8(MCHP_EMI_BASE(n) + 0x0)
#define MCHP_EMI_E2H_MBX(n) REG8(MCHP_EMI_BASE(n) + 0x1)
#define MCHP_EMI_MBA0(n) REG32(MCHP_EMI_BASE(n) + 0x4)
#define MCHP_EMI_MRL0(n) REG16(MCHP_EMI_BASE(n) + 0x8)
#define MCHP_EMI_MWL0(n) REG16(MCHP_EMI_BASE(n) + 0xa)
#define MCHP_EMI_MBA1(n) REG32(MCHP_EMI_BASE(n) + 0xc)
#define MCHP_EMI_MRL1(n) REG16(MCHP_EMI_BASE(n) + 0x10)
#define MCHP_EMI_MWL1(n) REG16(MCHP_EMI_BASE(n) + 0x12)
#define MCHP_EMI_ISR(n) REG16(MCHP_EMI_BASE(n) + 0x14)
#define MCHP_EMI_HCE(n) REG16(MCHP_EMI_BASE(n) + 0x16)
#define MCHP_EMI_ISR_B0(n) REG8(MCHP_EMI_RT_BASE(n) + 0x8)
#define MCHP_EMI_ISR_B1(n) REG8(MCHP_EMI_RT_BASE(n) + 0x9)
#define MCHP_EMI_IMR_B0(n) REG8(MCHP_EMI_RT_BASE(n) + 0xa)
#define MCHP_EMI_IMR_B1(n) REG8(MCHP_EMI_RT_BASE(n) + 0xb)

/* Mailbox */
#define MCHP_MBX_INDEX REG8(MCHP_MBX_RT_BASE + 0x0)
#define MCHP_MBX_DATA REG8(MCHP_MBX_RT_BASE + 0x1)
#define MCHP_MBX_H2E_MBX REG8(MCHP_MBX_BASE + 0x0)
#define MCHP_MBX_E2H_MBX REG8(MCHP_MBX_BASE + 0x4)
#define MCHP_MBX_ISR REG8(MCHP_MBX_BASE + 0x8)
#define MCHP_MBX_IMR REG8(MCHP_MBX_BASE + 0xc)
#define MCHP_MBX_REG(x) REG8(MCHP_MBX_BASE + 0x10 + (x))

/* PWM */
#define MCHP_PWM_ON(x) REG32(MCHP_PWM_BASE(x) + 0x00)
#define MCHP_PWM_OFF(x) REG32(MCHP_PWM_BASE(x) + 0x04)
#define MCHP_PWM_CFG(x) REG32(MCHP_PWM_BASE(x) + 0x08)

/* TACH */
#define MCHP_TACH_CTRL(x) REG32(MCHP_TACH_BASE(x))
#define MCHP_TACH_CTRL_LO(x) REG16(MCHP_TACH_BASE(x) + 0x00)
#define MCHP_TACH_CTRL_CNT(x) REG16(MCHP_TACH_BASE(x) + 0x02)
#define MCHP_TACH_STATUS(x) REG8(MCHP_TACH_BASE(x) + 0x04)
#define MCHP_TACH_LIMIT_HI(x) REG16(MCHP_TACH_BASE(x) + 0x08)
#define MCHP_TACH_LIMIT_LO(x) REG16(MCHP_TACH_BASE(x) + 0x0C)

/* ACPI */
#define MCHP_ACPI_EC_EC2OS(x, y) REG8(MCHP_ACPI_EC_BASE(x) + 0x100 + (y))
#define MCHP_ACPI_EC_STATUS(x) REG8(MCHP_ACPI_EC_BASE(x) + 0x104)
#define MCHP_ACPI_EC_BYTE_CTL(x) REG8(MCHP_ACPI_EC_BASE(x) + 0x105)
#define MCHP_ACPI_EC_OS2EC(x, y) REG8(MCHP_ACPI_EC_BASE(x) + 0x108 + (y))
#define MCHP_ACPI_PM1_STS1 REG8(MCHP_ACPI_PM_RT_BASE + 0x0)
#define MCHP_ACPI_PM1_STS2 REG8(MCHP_ACPI_PM_RT_BASE + 0x1)
#define MCHP_ACPI_PM1_EN1 REG8(MCHP_ACPI_PM_RT_BASE + 0x2)
#define MCHP_ACPI_PM1_EN2 REG8(MCHP_ACPI_PM_RT_BASE + 0x3)
#define MCHP_ACPI_PM1_CTL1 REG8(MCHP_ACPI_PM_RT_BASE + 0x4)
#define MCHP_ACPI_PM1_CTL2 REG8(MCHP_ACPI_PM_RT_BASE + 0x5)
#define MCHP_ACPI_PM2_CTL1 REG8(MCHP_ACPI_PM_RT_BASE + 0x6)
#define MCHP_ACPI_PM2_CTL2 REG8(MCHP_ACPI_PM_RT_BASE + 0x7)
#define MCHP_ACPI_PM_STS REG8(MCHP_ACPI_PM_EC_BASE + 0x10)

/* 8042 */
#define MCHP_8042_OBF_CLR REG8(MCHP_8042_BASE + 0x0)
#define MCHP_8042_H2E REG8(MCHP_8042_BASE + 0x100)
#define MCHP_8042_E2H REG8(MCHP_8042_BASE + 0x100)
#define MCHP_8042_STS REG8(MCHP_8042_BASE + 0x104)
#define MCHP_8042_KB_CTRL REG8(MCHP_8042_BASE + 0x108)
#define MCHP_8042_PCOBF REG8(MCHP_8042_BASE + 0x114)
#define MCHP_8042_ACT REG8(MCHP_8042_BASE + 0x330)

/* PROCHOT */
#define MCHP_PCHOT_CUM_CNT REG32(MCHP_PROCHOT_BASE + 0x00)
#define MCHP_PCHOT_DTY_CYC_CNT REG32(MCHP_PROCHOT_BASE + 0x04)
#define MCHP_PCHOT_DTY_PRD_CNT REG32(MCHP_PROCHOT_BASE + 0x08)
#define MCHP_PCHOT_STS_CTRL REG32(MCHP_PROCHOT_BASE + 0x0C)
#define MCHP_PCHOT_ASERT_CNT REG32(MCHP_PROCHOT_BASE + 0x10)
#define MCHP_PCHOT_ASERT_CNT_LMT REG32(MCHP_PROCHOT_BASE + 0x14)
#define MCHP_PCHOT_TEST REG32(MCHP_PROCHOT_BASE + 0x18)

/* I2C registers access given controller base address */
#define MCHP_I2C_CTRL(addr) REG8(addr)
#define MCHP_I2C_STATUS(addr) REG8(addr)
#define MCHP_I2C_OWN_ADDR(addr) REG16(addr + 0x4)
#define MCHP_I2C_DATA(addr) REG8(addr + 0x8)
#define MCHP_I2C_MASTER_CMD(addr) REG32(addr + 0xc)
#define MCHP_I2C_SLAVE_CMD(addr) REG32(addr + 0x10)
#define MCHP_I2C_PEC(addr) REG8(addr + 0x14)
#define MCHP_I2C_DATA_TIM_2(addr) REG8(addr + 0x18)
#define MCHP_I2C_COMPLETE(addr) REG32(addr + 0x20)
#define MCHP_I2C_IDLE_SCALE(addr) REG32(addr + 0x24)
#define MCHP_I2C_CONFIG(addr) REG32(addr + 0x28)
#define MCHP_I2C_BUS_CLK(addr) REG16(addr + 0x2c)
#define MCHP_I2C_BLK_ID(addr) REG8(addr + 0x30)
#define MCHP_I2C_REV(addr) REG8(addr + 0x34)
#define MCHP_I2C_BB_CTRL(addr) REG8(addr + 0x38)
#define MCHP_I2C_TST_DATA_TIM(addr) REG32(addr + 0x3c)
#define MCHP_I2C_DATA_TIM(addr) REG32(addr + 0x40)
#define MCHP_I2C_TOUT_SCALE(addr) REG32(addr + 0x44)
#define MCHP_I2C_SLAVE_TX_BUF(addr) REG8(addr + 0x48)
#define MCHP_I2C_SLAVE_RX_BUF(addr) REG8(addr + 0x4c)
#define MCHP_I2C_MASTER_TX_BUF(addr) REG8(addr + 0x50)
#define MCHP_I2C_MASTER_RX_BUF(addr) REG8(addr + 0x54)
#define MCHP_I2C_TEST_1(addr) REG32(addr + 0x58)
#define MCHP_I2C_TEST_2(addr) REG32(addr + 0x5c)
#define MCHP_I2C_WAKE_STS(addr) REG8(addr + 0x60)
#define MCHP_I2C_WAKE_EN(addr) REG8(addr + 0x64)
#define MCHP_I2C_TEST_3(addr) REG32(addr + 0x68)

/* Keyboard scan matrix */
#define MCHP_KS_KSO_SEL REG32(MCHP_KEYSCAN_BASE + 0x4)
#define MCHP_KS_KSI_INPUT REG32(MCHP_KEYSCAN_BASE + 0x8)
#define MCHP_KS_KSI_STATUS REG32(MCHP_KEYSCAN_BASE + 0xc)
#define MCHP_KS_KSI_INT_EN REG32(MCHP_KEYSCAN_BASE + 0x10)
#define MCHP_KS_EXT_CTRL REG32(MCHP_KEYSCAN_BASE + 0x14)

/* ADC */
#define MCHP_ADC_CTRL REG32(MCHP_ADC_BASE + 0x0)
#define MCHP_ADC_DELAY REG32(MCHP_ADC_BASE + 0x4)
#define MCHP_ADC_STS REG32(MCHP_ADC_BASE + 0x8)
#define MCHP_ADC_SINGLE REG32(MCHP_ADC_BASE + 0xc)
#define MCHP_ADC_REPEAT REG32(MCHP_ADC_BASE + 0x10)
#define MCHP_ADC_READ(x) REG32(MCHP_ADC_BASE + 0x14 + ((x) * 0x4))

/* Hibernation timer */
#define MCHP_HTIMER_PRELOAD(x) REG16(MCHP_HTIMER_ADDR(x) + 0x0)
#define MCHP_HTIMER_CONTROL(x) REG16(MCHP_HTIMER_ADDR(x) + 0x4)
#define MCHP_HTIMER_COUNT(x) REG16(MCHP_HTIMER_ADDR(x) + 0x8)

/* Week timer and BGPO control */
#define MCHP_WKTIMER_CTRL REG32(MCHP_WKTIMER_BASE + 0)
#define MCHP_WKTIMER_ALARM_CNT REG32(MCHP_WKTIMER_BASE + 0x04)
#define MCHP_WKTIMER_COMPARE REG32(MCHP_WKTIMER_BASE + 0x08)
#define MCHP_WKTIMER_CLK_DIV REG32(MCHP_WKTIMER_BASE + 0x0c)
#define MCHP_WKTIMER_SUBSEC_ISEL REG32(MCHP_WKTIMER_BASE + 0x10)
#define MCHP_WKTIMER_SUBWK_CTRL REG32(MCHP_WKTIMER_BASE + 0x14)
#define MCHP_WKTIMER_SUBWK_ALARM REG32(MCHP_WKTIMER_BASE + 0x18)
#define MCHP_WKTIMER_BGPO_DATA REG32(MCHP_WKTIMER_BASE + 0x1c)
#define MCHP_WKTIMER_BGPO_POWER REG32(MCHP_WKTIMER_BASE + 0x20)
#define MCHP_WKTIMER_BGPO_RESET REG32(MCHP_WKTIMER_BASE + 0x24)

/* Quad Master SPI (QMSPI) */
#define MCHP_QMSPI0_MODE REG32(MCHP_QMSPI0_BASE + 0x00)
#define MCHP_QMSPI0_MODE_ACT_SRST REG8(MCHP_QMSPI0_BASE + 0x00)
#define MCHP_QMSPI0_MODE_SPI_MODE REG8(MCHP_QMSPI0_BASE + 0x01)
#define MCHP_QMSPI0_MODE_FDIV REG8(MCHP_QMSPI0_BASE + 0x02)
#define MCHP_QMSPI0_CTRL REG32(MCHP_QMSPI0_BASE + 0x04)
#define MCHP_QMSPI0_EXE REG8(MCHP_QMSPI0_BASE + 0x08)
#define MCHP_QMSPI0_IFCTRL REG8(MCHP_QMSPI0_BASE + 0x0C)
#define MCHP_QMSPI0_STS REG32(MCHP_QMSPI0_BASE + 0x10)
#define MCHP_QMSPI0_BUFCNT_STS REG32(MCHP_QMSPI0_BASE + 0x14)
#define MCHP_QMSPI0_IEN REG32(MCHP_QMSPI0_BASE + 0x18)
#define MCHP_QMSPI0_BUFCNT_TRIG REG32(MCHP_QMSPI0_BASE + 0x1C)
#define MCHP_QMSPI0_TX_FIFO_ADDR (MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_TX_FIFO8 REG8(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_TX_FIFO16 REG16(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_TX_FIFO32 REG32(MCHP_QMSPI0_BASE + 0x20)
#define MCHP_QMSPI0_RX_FIFO_ADDR (MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_RX_FIFO8 REG8(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_RX_FIFO16 REG16(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_RX_FIFO32 REG32(MCHP_QMSPI0_BASE + 0x24)
#define MCHP_QMSPI0_DESCR(x) REG32(MCHP_QMSPI0_BASE + 0x30 + ((x) * 4))
/* Bits in MCHP_QMSPI0_MODE */
#define MCHP_QMSPI_M_ACTIVATE BIT(0)
#define MCHP_QMSPI_M_SOFT_RESET BIT(1)
#define MCHP_QMSPI_M_SPI_MODE_MASK (0x7ul << 8)
#define MCHP_QMSPI_M_SPI_MODE0 (0x0ul << 8)
#define MCHP_QMSPI_M_SPI_MODE3 (0x3ul << 8)
#define MCHP_QMSPI_M_SPI_MODE0_48M (0x4ul << 8)
#define MCHP_QMSPI_M_SPI_MODE3_48M (0x7ul << 8)
/*
 * clock divider is 8-bit field in bits[23:16]
 * [1, 255] -> 48MHz / [1, 255], 0 -> 48MHz / 256
 */
#define MCHP_QMSPI_M_CLKDIV_BITPOS 16
#define MCHP_QMSPI_M_CLKDIV_48M (1ul << 16)
#define MCHP_QMSPI_M_CLKDIV_24M (2ul << 16)
#define MCHP_QMSPI_M_CLKDIV_16M (3ul << 16)
#define MCHP_QMSPI_M_CLKDIV_12M (4ul << 16)
#define MCHP_QMSPI_M_CLKDIV_8M (6ul << 16)
#define MCHP_QMSPI_M_CLKDIV_6M (8ul << 16)
#define MCHP_QMSPI_M_CLKDIV_1M (48ul << 16)
#define MCHP_QMSPI_M_CLKDIV_188K (0x100ul << 16)
/* Bits in MCHP_QMSPI0_CTRL and MCHP_QMSPI_DESCR(x) */
#define MCHP_QMSPI_C_1X (0ul << 0) /* Full Duplex */
#define MCHP_QMSPI_C_2X (1ul << 0) /* Dual IO */
#define MCHP_QMSPI_C_4X (2ul << 0) /* Quad IO */
#define MCHP_QMSPI_C_TX_DIS (0ul << 2)
#define MCHP_QMSPI_C_TX_DATA (1ul << 2)
#define MCHP_QMSPI_C_TX_ZEROS (2ul << 2)
#define MCHP_QMSPI_C_TX_ONES (3ul << 2)
#define MCHP_QMSPI_C_TX_DMA_DIS (0ul << 4)
#define MCHP_QMSPI_C_TX_DMA_1B (1ul << 4)
#define MCHP_QMSPI_C_TX_DMA_2B (2ul << 4)
#define MCHP_QMSPI_C_TX_DMA_4B (3ul << 4)
#define MCHP_QMSPI_C_TX_DMA_MASK (3ul << 4)
#define MCHP_QMSPI_C_RX_DIS 0
#define MCHP_QMSPI_C_RX_EN BIT(6)
#define MCHP_QMSPI_C_RX_DMA_DIS (0ul << 7)
#define MCHP_QMSPI_C_RX_DMA_1B (1ul << 7)
#define MCHP_QMSPI_C_RX_DMA_2B (2ul << 7)
#define MCHP_QMSPI_C_RX_DMA_4B (3ul << 7)
#define MCHP_QMSPI_C_RX_DMA_MASK (3ul << 7)
#define MCHP_QMSPI_C_NO_CLOSE 0
#define MCHP_QMSPI_C_CLOSE BIT(9)
#define MCHP_QMSPI_C_XFRU_BITS (0ul << 10)
#define MCHP_QMSPI_C_XFRU_1B (1ul << 10)
#define MCHP_QMSPI_C_XFRU_4B (2ul << 10)
#define MCHP_QMSPI_C_XFRU_16B (3ul << 10)
#define MCHP_QMSPI_C_XFRU_MASK (3ul << 10)
/* Control */
#define MCHP_QMSPI_C_START_DESCR_BITPOS 12
#define MCHP_QMSPI_C_START_DESCR_MASK (0xFul << 12)
#define MCHP_QMSPI_C_DESCR_MODE_EN BIT(16)
/* Descriptors, indicates the current descriptor is the last */
#define MCHP_QMSPI_C_NEXT_DESCR_BITPOS 12
#define MCHP_QMSPI_C_NEXT_DESCR_MASK0 0xFul
#define MCHP_QMSPI_C_NEXT_DESCR_MASK ((MCHP_QMSPI_C_NEXT_DESCR_MASK0) << 12)
#define MCHP_QMSPI_C_NXTD(n) ((n) << 12)
#define MCHP_QMSPI_C_DESCR_LAST BIT(16)
/*
 * Total transfer length is the count in this field
 * scaled by units in MCHP_QMSPI_CTRL_XFRU_xxxx
 */
#define MCHP_QMSPI_C_NUM_UNITS_BITPOS 17
#define MCHP_QMSPI_C_MAX_UNITS 0x7ffful
#define MCHP_QMSPI_C_NUM_UNITS_MASK0 0x7ffful
#define MCHP_QMSPI_C_NUM_UNITS_MASK ((MCHP_QMSPI_C_NUM_UNITS_MASK0) << 17)
/* Bits in MCHP_QMSPI0_EXE */
#define MCHP_QMSPI_EXE_START BIT(0)
#define MCHP_QMSPI_EXE_STOP BIT(1)
#define MCHP_QMSPI_EXE_CLR_FIFOS BIT(2)
/* MCHP QMSPI FIFO Sizes */
#define MCHP_QMSPI_TX_FIFO_LEN 8
#define MCHP_QMSPI_RX_FIFO_LEN 8
/* Bits in MCHP_QMSPI0_STS and MCHP_QMSPI0_IEN */
#define MCHP_QMSPI_STS_DONE BIT(0)
#define MCHP_QMSPI_STS_DMA_DONE BIT(1)
#define MCHP_QMSPI_STS_TX_BUFF_ERR BIT(2)
#define MCHP_QMSPI_STS_RX_BUFF_ERR BIT(3)
#define MCHP_QMSPI_STS_PROG_ERR BIT(4)
#define MCHP_QMSPI_STS_TX_BUFF_FULL BIT(8)
#define MCHP_QMSPI_STS_TX_BUFF_EMPTY BIT(9)
#define MCHP_QMSPI_STS_TX_BUFF_REQ BIT(10)
#define MCHP_QMSPI_STS_TX_BUFF_STALL BIT(11) /* status only */
#define MCHP_QMSPI_STS_RX_BUFF_FULL BIT(12)
#define MCHP_QMSPI_STS_RX_BUFF_EMPTY BIT(13)
#define MCHP_QMSPI_STS_RX_BUFF_REQ BIT(14)
#define MCHP_QMSPI_STS_RX_BUFF_STALL BIT(15) /* status only */
#define MCHP_QMSPI_STS_ACTIVE BIT(16) /* status only */
/* Bits in MCHP_QMSPI0_BUFCNT (read-only) */
#define MCHP_QMSPI_BUFCNT_TX_BITPOS 0
#define MCHP_QMSPI_BUFCNT_TX_MASK 0xFFFFul
#define MCHP_QMSPI_BUFCNT_RX_BITPOS 16
#define MCHP_QMSPI_BUFCNT_RX_MASK (0xFFFFul << 16)
#define MCHP_QMSPI0_ID 0

/* eSPI */
/* eSPI IO Component */
/* Peripheral Channel Registers */
#define MCHP_ESPI_PC_STATUS REG32(MCHP_ESPI_IO_BASE + 0x114)
#define MCHP_ESPI_PC_IEN REG32(MCHP_ESPI_IO_BASE + 0x118)
#define MCHP_ESPI_PC_BAR_INHIBIT_LO REG32(MCHP_ESPI_IO_BASE + 0x120)
#define MCHP_ESPI_PC_BAR_INHIBIT_HI REG32(MCHP_ESPI_IO_BASE + 0x124)
#define MCHP_ESPI_PC_BAR_INIT_LD_0C REG16(MCHP_ESPI_IO_BASE + 0x128)
#define MCHP_ESPI_PC_EC_IRQ REG8(MCHP_ESPI_IO_BASE + 0x12C)
/* LTR Registers */
#define MCHP_ESPI_IO_LTR_STATUS REG16(MCHP_ESPI_IO_BASE + 0x220)
#define MCHP_ESPI_IO_LTR_IEN REG8(MCHP_ESPI_IO_BASE + 0x224)
#define MCHP_ESPI_IO_LTR_CTRL REG16(MCHP_ESPI_IO_BASE + 0x228)
#define MCHP_ESPI_IO_LTR_MSG REG16(MCHP_ESPI_IO_BASE + 0x22C)
/* OOB Channel Registers */
#define MCHP_ESPI_OOB_RX_ADDR_LO REG32(MCHP_ESPI_IO_BASE + 0x240)
#define MCHP_ESPI_OOB_RX_ADDR_HI REG32(MCHP_ESPI_IO_BASE + 0x244)
#define MCHP_ESPI_OOB_TX_ADDR_LO REG32(MCHP_ESPI_IO_BASE + 0x248)
#define MCHP_ESPI_OOB_TX_ADDR_HI REG32(MCHP_ESPI_IO_BASE + 0x24C)
#define MCHP_ESPI_OOB_RX_LEN REG32(MCHP_ESPI_IO_BASE + 0x250)
#define MCHP_ESPI_OOB_TX_LEN REG32(MCHP_ESPI_IO_BASE + 0x254)
#define MCHP_ESPI_OOB_RX_CTL REG32(MCHP_ESPI_IO_BASE + 0x258)
#define MCHP_ESPI_OOB_RX_IEN REG8(MCHP_ESPI_IO_BASE + 0x25C)
#define MCHP_ESPI_OOB_RX_STATUS REG32(MCHP_ESPI_IO_BASE + 0x260)
#define MCHP_ESPI_OOB_TX_CTL REG32(MCHP_ESPI_IO_BASE + 0x264)
#define MCHP_ESPI_OOB_TX_IEN REG8(MCHP_ESPI_IO_BASE + 0x268)
#define MCHP_ESPI_OOB_TX_STATUS REG32(MCHP_ESPI_IO_BASE + 0x26C)
/* Flash Channel Registers */
#define MCHP_ESPI_FC_ADDR_LO REG32(MCHP_ESPI_IO_BASE + 0x280)
#define MCHP_ESPI_FC_ADDR_HI REG32(MCHP_ESPI_IO_BASE + 0x284)
#define MCHP_ESPI_FC_BUF_ADDR_LO REG32(MCHP_ESPI_IO_BASE + 0x288)
#define MCHP_ESPI_FC_BUF_ADDR_HI REG32(MCHP_ESPI_IO_BASE + 0x28C)
#define MCHP_ESPI_FC_XFR_LEN REG32(MCHP_ESPI_IO_BASE + 0x290)
#define MCHP_ESPI_FC_CTL REG32(MCHP_ESPI_IO_BASE + 0x294)
#define MCHP_ESPI_FC_IEN REG8(MCHP_ESPI_IO_BASE + 0x298)
#define MCHP_ESPI_FC_CONFIG REG32(MCHP_ESPI_IO_BASE + 0x29C)
#define MCHP_ESPI_FC_STATUS REG32(MCHP_ESPI_IO_BASE + 0x2A0)
/* VWire Channel Registers */
#define MCHP_ESPI_VW_STATUS REG8(MCHP_ESPI_IO_BASE + 0x2B0)
/* Global Registers */
/* 32-bit register containing CAP_ID/CAP0/CAP1/PC_CAP */
#define MCHP_ESPI_IO_REG32_A REG32(MCHP_ESPI_IO_BASE + 0x2E0)
#define MCHP_ESPI_IO_CAP_ID REG8(MCHP_ESPI_IO_BASE + 0x2E0)
#define MCHP_ESPI_IO_CAP0 REG8(MCHP_ESPI_IO_BASE + 0x2E1)
#define MCHP_ESPI_IO_CAP1 REG8(MCHP_ESPI_IO_BASE + 0x2E2)
#define MCHP_ESPI_IO_PC_CAP REG8(MCHP_ESPI_IO_BASE + 0x2E3)
/* 32-bit register containing VW_CAP/OOB_CAP/FC_CAP/PC_READY */
#define MCHP_ESPI_IO_REG32_B REG32(MCHP_ESPI_IO_BASE + 0x2E4)
#define MCHP_ESPI_IO_VW_CAP REG8(MCHP_ESPI_IO_BASE + 0x2E4)
#define MCHP_ESPI_IO_OOB_CAP REG8(MCHP_ESPI_IO_BASE + 0x2E5)
#define MCHP_ESPI_IO_FC_CAP REG8(MCHP_ESPI_IO_BASE + 0x2E6)
#define MCHP_ESPI_IO_PC_READY REG8(MCHP_ESPI_IO_BASE + 0x2E7)
/* 32-bit register containing OOB_READY/FC_READY/RESET_STATUS/RESET_IEN */
#define MCHP_ESPI_IO_REG32_C REG32(MCHP_ESPI_IO_BASE + 0x2E8)
#define MCHP_ESPI_IO_OOB_READY REG8(MCHP_ESPI_IO_BASE + 0x2E8)
#define MCHP_ESPI_IO_FC_READY REG8(MCHP_ESPI_IO_BASE + 0x2E9)
#define MCHP_ESPI_IO_RESET_STATUS REG8(MCHP_ESPI_IO_BASE + 0x2EA)
#define MCHP_ESPI_IO_RESET_IEN REG8(MCHP_ESPI_IO_BASE + 0x2EB)
/* 32-bit register containing PLTRST_SRC/VW_READY */
#define MCHP_ESPI_IO_REG32_D REG32(MCHP_ESPI_IO_BASE + 0x2EC)
#define MCHP_ESPI_IO_PLTRST_SRC REG8(MCHP_ESPI_IO_BASE + 0x2EC)
#define MCHP_ESPI_IO_VW_READY REG8(MCHP_ESPI_IO_BASE + 0x2ED)
/* Bits in MCHP_ESPI_IO_CAP0 */
#define MCHP_ESPI_CAP0_PC_SUPP 0x01
#define MCHP_ESPI_CAP0_VW_SUPP 0x02
#define MCHP_ESPI_CAP0_OOB_SUPP 0x04
#define MCHP_ESPI_CAP0_FC_SUPP 0x08
#define MCHP_ESPI_CAP0_ALL_CHAN_SUPP                       \
	(MCHP_ESPI_CAP0_PC_SUPP | MCHP_ESPI_CAP0_VW_SUPP | \
	 MCHP_ESPI_CAP0_OOB_SUPP | MCHP_ESPI_CAP0_FC_SUPP)
/* Bits in MCHP_ESPI_IO_CAP1 */
#define MCHP_ESPI_CAP1_RW_MASK 0x37
#define MCHP_ESPI_CAP1_MAX_FREQ_MASK 0x07
#define MCHP_ESPI_CAP1_MAX_FREQ_20M 0
#define MCHP_ESPI_CAP1_MAX_FREQ_25M 1
#define MCHP_ESPI_CAP1_MAX_FREQ_33M 2
#define MCHP_ESPI_CAP1_MAX_FREQ_50M 3
#define MCHP_ESPI_CAP1_MAX_FREQ_66M 4
#define MCHP_ESPI_CAP1_SINGLE_MODE 0
#define MCHP_ESPI_CAP1_SINGLE_DUAL_MODE BIT(0)
#define MCHP_ESPI_CAP1_SINGLE_QUAD_MODE BIT(1)
#define MCHP_ESPI_CAP1_ALL_MODE                                         \
	(MCHP_ESPI_CAP1_SINGLE_MODE | MCHP_ESPI_CAP1_SINGLE_DUAL_MODE | \
	 MCHP_ESPI_CAP1_SINGLE_QUAD_MODE)
#define MCHP_ESPI_CAP1_IO_BITPOS 4
#define MCHP_ESPI_CAP1_IO_MASK0 0x03
#define MCHP_ESPI_CAP1_IO_MASK (0x03ul << MCHP_ESPI_CAP1_IO_BITPOS)
#define MCHP_ESPI_CAP1_IO1_VAL 0x00
#define MCHP_ESPI_CAP1_IO12_VAL 0x01
#define MCHP_ESPI_CAP1_IO24_VAL 0x02
#define MCHP_ESPI_CAP1_IO124_VAL 0x03
#define MCHP_ESPI_CAP1_IO1 (0x00 << 4)
#define MCHP_ESPI_CAP1_IO12 (0x01 << 4)
#define MCHP_ESPI_CAP1_IO24 (0x02 << 4)
#define MCHP_ESPI_CAP1_IO124 (0x03 << 4)
/* Bits in MCHP_ESPI_IO_RESET_STATUS and MCHP_ESPI_IO_RESET_IEN */
#define MCHP_ESPI_RST_PIN_MASK BIT(1)
#define MCHP_ESPI_RST_CHG_STS BIT(0)
#define MCHP_ESPI_RST_IEN BIT(0)
/* Bits in MCHP_ESPI_IO_PLTRST_SRC */
#define MCHP_ESPI_PLTRST_SRC_VW 0
#define MCHP_ESPI_PLTRST_SRC_PIN 1
/*
 * eSPI Slave Activate Register
 * bit[0] = 0 de-active block is clock-gates
 * bit[0] = 1 block is powered and functional
 */
#define MCHP_ESPI_ACTIVATE REG8(MCHP_ESPI_IO_BASE + 0x330)
/*
 * IO BAR's starting at offset 0x134
 * b[16]=virtualized R/W
 * b[15:14]=0 reserved RO
 * b[13:8]=Logical Device Number RO
 * b[7:0]=mask
 */
#define MCHP_ESPI_IO_BAR_CTL(x) REG32(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x134)
/* access mask field of eSPI IO BAR Control register */
#define MCHP_ESPI_IO_BAR_CTL_MASK(x) REG8(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x134)
/*
 * IO BAR's starting at offset 0x334
 * b[31:16] = I/O address
 * b[15:1]=0 reserved
 * b[0] = valid
 */
#define MCHP_ESPI_IO_BAR(x) REG32(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x334)
#define MCHP_ESPI_IO_BAR_VALID(x) REG8(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x334)
#define MCHP_ESPI_IO_BAR_ADDR_LSB(x) REG8(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x336)
#define MCHP_ESPI_IO_BAR_ADDR_MSB(x) REG8(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x337)
#define MCHP_ESPI_IO_BAR_ADDR(x) REG16(MCHP_ESPI_IO_BASE + ((x) * 4) + 0x336)
/* eSPI Serial IRQ registers */
#define MCHP_ESPI_IO_SERIRQ_REG(x) REG8(MCHP_ESPI_IO_BASE + 0x3ac + (x))
/* eSPI Virtual Wire Error Register */
#define MCHP_ESPI_IO_VW_ERROR REG8(MCHP_ESPI_IO_BASE + 0x3f0)
/*
 * eSPI Logical Device Memory Host BAR's to specify Host memory
 * base address and valid bit.
 * Each Logical Device implementing memory access has an 80-bit register.
 * b[0]=Valid
 * b[15:1]=0(reserved)
 * b[79:16]=eSPI bus memory address(Host address space)
 */
#define MCHP_ESPI_MBAR_VALID(x) REG8(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x130)
#define MCHP_ESPI_MBAR_HOST_ADDR_0_15(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x132)
#define MCHP_ESPI_MBAR_HOST_ADDR_16_31(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x134)
#define MCHP_ESPI_MBAR_HOST_ADDR_32_47(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x136)
#define MCHP_ESPI_MBAR_HOST_ADDR_48_63(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x138)
/*
 * eSPI SRAM BAR's
 * b[0,3,8:15] = 0 reserved
 * b[2:1] = access
 * b[7:4] = size
 * b[79:16] = Host address
 */
#define MCHP_ESPI_SRAM_BAR_CFG(x) REG8(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x1ac)
#define MCHP_ESPI_SRAM_BAR_ADDR_0_15(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x1ae)
#define MCHP_ESPI_SRAM_BAR_ADDR_16_31(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x1b0)
#define MCHP_ESPI_SRAM_BAR_ADDR_32_47(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x1b2)
#define MCHP_ESPI_SRAM_BAR_ADDR_48_63(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x1b4)
/* eSPI Memory Bus Master Registers */
#define MCHP_ESPI_BM_STATUS REG32(MCHP_ESPI_MEM_BASE + 0x200)
#define MCHP_ESPI_BM_IEN REG32(MCHP_ESPI_MEM_BASE + 0x204)
#define MCHP_ESPI_BM_CONFIG REG32(MCHP_ESPI_MEM_BASE + 0x208)
#define MCHP_ESPI_BM1_CTL REG32(MCHP_ESPI_MEM_BASE + 0x210)
#define MCHP_ESPI_BM1_HOST_ADDR_LO REG32(MCHP_ESPI_MEM_BASE + 0x214)
#define MCHP_ESPI_BM1_HOST_ADDR_HI REG32(MCHP_ESPI_MEM_BASE + 0x218)
#define MCHP_ESPI_BM1_EC_ADDR REG32(MCHP_ESPI_MEM_BASE + 0x21c)
#define MCHP_ESPI_BM2_CTL REG32(MCHP_ESPI_MEM_BASE + 0x224)
#define MCHP_ESPI_BM2_HOST_ADDR_LO REG32(MCHP_ESPI_MEM_BASE + 0x228)
#define MCHP_ESPI_BM2_HOST_ADDR_HI REG32(MCHP_ESPI_MEM_BASE + 0x22c)
#define MCHP_ESPI_BM2_EC_ADDR REG32(MCHP_ESPI_MEM_BASE + 0x230)
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
	REG32(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x330)
#define MCHP_ESPI_MBAR_EC_ADDR_0_15(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x332)
#define MCHP_ESPI_MBAR_EC_ADDR_16_31(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x334)
#define MCHP_ESPI_MBAR_EC_ADDR_32_47(x) \
	REG16(MCHP_ESPI_MEM_BASE + ((x) * 10) + 0x336)

/* eSPI Virtual Wire registers */
#define MCHP_ESPI_MSVW_LEN 12
#define MCHP_ESPI_SMVW_LEN 8

#define MCHP_ESPI_MSVW_ADDR(n) \
	((MCHP_ESPI_MSVW_BASE) + ((n) * (MCHP_ESPI_MSVW_LEN)))

#define MCHP_ESPI_MSVW_MTOS_BITPOS 4

#define MCHP_ESPI_MSVW_IRQSEL_LEVEL_LO 0
#define MCHP_ESPI_MSVW_IRQSEL_LEVEL_HI 1
#define MCHP_ESPI_MSVW_IRQSEL_DISABLED 4
#define MCHP_ESPI_MSVW_IRQSEL_RISING 0x0d
#define MCHP_ESPI_MSVW_IRQSEL_FALLING 0x0e
#define MCHP_ESPI_MSVW_IRQSEL_BOTH_EDGES 0x0f

/*
 * Mapping of eSPI Master Host VWire group indices to
 * MCHP eSPI Master to Slave 96-bit VWire registers.
 * MSVW_xy where xy = PCH VWire number.
 * Each PCH VWire number controls 4 virtual wires.
 */
#define MSVW_H02 0
#define MSVW_H03 1
#define MSVW_H07 2
#define MSVW_H41 3
#define MSVW_H42 4
#define MSVW_H43 5
#define MSVW_H44 6
#define MSVW_H47 7
#define MSVW_H4A 8
#define MSVW_HSPARE0 9
#define MSVW_HSPARE1 10
#define MSVW_MAX 11

/* Access 32-bit word in 96-bit MSVW register. 0 <= w <= 2 */
#define MSVW(id, w) \
	REG32(MCHP_ESPI_MSVW_BASE + ((id) * 12) + (((w) & 0x03) * 4))
/* Access index value in byte 0 */
#define MCHP_ESPI_VW_M2S_INDEX(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12))
/*
 * Access MTOS_SOURCE and MTOS_STATE in byte 1
 * MTOS_SOURCE = b[1:0] specifies reset source
 * MTOS_STATE = b[7:4] are states loaded into SRC[0:3] on reset event
 */
#define MCHP_ESPI_VW_M2S_MTOS(id) REG8(MCHP_ESPI_VW_BASE + 1 + ((id) * 12))
/*
 * Access Index, MTOS Source, and MTOS State as 16-bit quantity.
 * Index in b[7:0]
 * MTOS Source in b[9:8]
 * MTOS State in b[15:12]
 */
#define MCHP_ESPI_VW_M2S_INDEX_MTOS(id) REG16(MCHP_ESPI_VW_BASE + ((id) * 12))
/* Access SRCn IRQ Select bit fields */
#define MCHP_ESPI_VW_M2S_IRQSEL0(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 4)
#define MCHP_ESPI_VW_M2S_IRQSEL1(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 5)
#define MCHP_ESPI_VW_M2S_IRQSEL2(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 6)
#define MCHP_ESPI_VW_M2S_IRQSEL3(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 7)
#define MCHP_ESPI_VW_M2S_IRQSEL(id, src) \
	REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 4 + ((src) & 0x03))
#define MCHP_ESPI_VW_M2S_IRQSEL_ALL(id) \
	REG32(MCHP_ESPI_VW_BASE + ((id) * 12) + 4)
/* Access individual source bits */
#define MCHP_ESPI_VW_M2S_SRC0(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 8)
#define MCHP_ESPI_VW_M2S_SRC1(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 9)
#define MCHP_ESPI_VW_M2S_SRC2(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 10)
#define MCHP_ESPI_VW_M2S_SRC3(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 12) + 11)
/*
 * Access all four Source bits as 32-bit value, Source bits are located
 * at bits[0, 8, 16, 24] of 32-bit word.
 */
#define MCHP_ESPI_VW_M2S_SRC_ALL(id) REG32(MCHP_ESPI_VW_BASE + 8 + ((id) * 12))
/*
 * Access an individual Source bit as byte where
 * bit[0] contains the source bit.
 */
#define MCHP_ESPI_VW_M2S_SRC(id, src) \
	REG8(MCHP_ESPI_VW_BASE + 8 + ((id) * 8) + ((src) & 0x03))

/*
 * Indices of Slave to Master Virtual Wire registers.
 * Registers are 64-bit.
 * Host chipset groups VWires into groups of 4 with
 * a spec. defined index.
 * SMVW_Ixy where xy = eSPI Master defined index.
 * MCHP maps Host indices into its Slave to Master
 * 64-bit registers.
 */
#define SMVW_H04 0
#define SMVW_H05 1
#define SMVW_H06 2
#define SMVW_H40 3
#define SMVW_H45 4
#define SMVW_H46 5
#define SMVW_HSPARE6 6
#define SMVW_HSPARE7 7
#define SMVW_HSPARE8 8
#define SMVW_HSPARE9 9
#define SMVW_HSPARE10 10
#define SMVW_MAX 11

/* Access 32-bit word of 64-bit SMVW register, 0 <= w <= 1 */
#define SMVW(id, w) \
	REG32(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x200 + (((w) & 0x01) * 4))
/* Access Index in b[7:0] of byte 0 */
#define MCHP_ESPI_VW_S2M_INDEX(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x200)
/* Access STOM_SOURCE and STOM_STATE in byte 1
 * STOM_SOURCE = b[1:0]
 * STOM_STATE = b[7:4]
 */
#define MCHP_ESPI_VW_S2M_STOM(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x201)
/* Access Index, STOM_SOURCE, and STOM_STATE in bytes[1:0]
 * Index = b[7:0]
 * STOM_SOURCE = b[9:8]
 * STOM_STATE = [15:12]
 */
#define MCHP_ESPI_VW_S2M_INDEX_STOM(id) \
	REG16(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x200)
/* Access Change[0:3] RO bits. Set to 1 if any of SRC[0:3] change */
#define MCHP_ESPI_VW_S2M_CHANGE(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x202)
/* Access individual SRC bits
 * bit[0] = SRCn
 */
#define MCHP_ESPI_VW_S2M_SRC0(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x204)
#define MCHP_ESPI_VW_S2M_SRC1(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x205)
#define MCHP_ESPI_VW_S2M_SRC2(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x206)
#define MCHP_ESPI_VW_S2M_SRC3(id) REG8(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x207)
/*
 * Access specified source bit as byte read/write.
 * Source bit is in bit[0] of byte.
 */
#define MCHP_ESPI_VW_S2M_SRC(id, src) \
	REG8(MCHP_ESPI_VW_BASE + 0x204 + ((id) * 8) + ((src) & 0x03))
/* Access SRC[0:3] as 32-bit word
 * SRC0 = b[0]
 * SRC1 = b[8]
 * SRC2 = b[16]
 * SRC3 = b[24]
 */
#define MCHP_ESPI_VW_S2M_SRC_ALL(id) \
	REG32(MCHP_ESPI_VW_BASE + ((id) * 8) + 0x204)

/* DMA */
#define MCHP_DMA_MAIN_CTRL REG8(MCHP_DMA_BASE + 0x00)
#define MCHP_DMA_MAIN_PKT_RO REG32(MCHP_DMA_BASE + 0x04)
#define MCHP_DMA_MAIN_FSM_RO REG8(MCHP_DMA_BASE + 0x08)
/* DMA Channel Registers */
#define MCHP_DMA_CH_ACT(n) REG8(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS))
#define MCHP_DMA_CH_MEM_START(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x04)
#define MCHP_DMA_CH_MEM_END(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x08)
#define MCHP_DMA_CH_DEV_ADDR(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x0c)
#define MCHP_DMA_CH_CTRL(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x10)
#define MCHP_DMA_CH_ISTS(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x14)
#define MCHP_DMA_CH_IEN(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x18)
#define MCHP_DMA_CH_FSM_RO(n) \
	REG32(MCHP_DMA_CH_BASE + ((n) * MCHP_DMA_CH_OFS) + 0x1c)
/*
 * DMA Channel 0 implements CRC-32 feature
 */
#define MCHP_DMA_CH0_CRC32_EN REG8(MCHP_DMA_CH_BASE + 0x20)
#define MCHP_DMA_CH0_CRC32_DATA REG32(MCHP_DMA_CH_BASE + 0x24)
#define MCHP_DMA_CH0_CRC32_POST_STS REG8(MCHP_DMA_CH_BASE + 0x28)
/*
 * DMA Channel 1 implements memory fill feature
 */
#define MCHP_DMA_CH1_FILL_EN REG8(MCHP_DMA_CH_BASE + MCHP_DMA_CH_OFS + 0x20)
#define MCHP_DMA_CH1_FILL_DATA REG32(MCHP_DMA_CH_BASE + MCHP_DMA_CH_OFS + 0x24)
/* Bits for DMA Main Control */
#define MCHP_DMA_MAIN_CTRL_ACT BIT(0)
#define MCHP_DMA_MAIN_CTRL_SRST BIT(1)
/* Bits for DMA channel regs */
#define MCHP_DMA_ACT_EN BIT(0)
/* DMA Channel Control */
#define MCHP_DMA_ABORT BIT(25)
#define MCHP_DMA_SW_GO BIT(24)
#define MCHP_DMA_XFER_SIZE_MASK (7ul << 20)
#define MCHP_DMA_XFER_SIZE(x) ((x) << 20)
#define MCHP_DMA_DIS_HW_FLOW BIT(19)
#define MCHP_DMA_INC_DEV BIT(17)
#define MCHP_DMA_INC_MEM BIT(16)
#define MCHP_DMA_DEV(x) ((x) << 9)
#define MCHP_DMA_DEV_MASK0 (0x7f)
#define MCHP_DMA_DEV_MASK (0x7f << 9)
#define MCHP_DMA_TO_DEV BIT(8)
#define MCHP_DMA_DONE BIT(2)
#define MCHP_DMA_RUN BIT(0)
/* DMA Channel Status */
#define MCHP_DMA_STS_ALU_DONE BIT(3)
#define MCHP_DMA_STS_DONE BIT(2)
#define MCHP_DMA_STS_HWFL_ERR BIT(1)
#define MCHP_DMA_STS_BUS_ERR BIT(0)

/*
 * Required structure typedef for common/dma.h interface
 * !!! checkpatch.pl will not like this !!!
 * structure moved to chip level dma.c
 * We can't remove dma_chan_t as its used in DMA API header.
 */
struct MCHP_dma_chan {
	uint32_t act; /* Activate */
	uint32_t mem_start; /* Memory start address */
	uint32_t mem_end; /* Memory end address */
	uint32_t dev; /* Device address */
	uint32_t ctrl; /* Control */
	uint32_t int_status; /* Interrupt status */
	uint32_t int_enabled; /* Interrupt enabled */
	uint32_t chfsm; /* channel fsm read-only */
	uint32_t alu_en; /* channels 0 & 1 only */
	uint32_t alu_data; /* channels 0 & 1 only */
	uint32_t alu_sts; /* channel 0 only */
	uint32_t alu_ro; /* channel 0 only */
	uint32_t rsvd[4]; /* 0x30 - 0x3F */
};

/* Common code and header file must use this */
typedef struct MCHP_dma_chan dma_chan_t;

/* Wake pin definitions, defined at board-level */
extern const enum gpio_signal hibernate_wake_pins[];
extern const int hibernate_wake_pins_used;
#endif /* __CROS_EC_REGISTERS_H */
