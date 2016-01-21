/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for MEC1322 processor
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"

/* Helper function for RAM address aliasing */
#define MEC1322_RAM_ALIAS(x)   \
	((x) >= 0x118000 ? (x) - 0x118000 + 0x20000000 : (x))

/* EC Chip Configuration */
#define MEC1322_CHIP_BASE      0x400fff00
#define MEC1322_CHIP_DEV_ID    REG8(MEC1322_CHIP_BASE + 0x20)
#define MEC1322_CHIP_DEV_REV   REG8(MEC1322_CHIP_BASE + 0x21)


/* Power/Clocks/Resets */
#define MEC1322_PCR_BASE         0x40080100
#define MEC1322_PCR_CHIP_SLP_EN  REG32(MEC1322_PCR_BASE + 0x0)
#define MEC1322_PCR_CHIP_CLK_REQ REG32(MEC1322_PCR_BASE + 0x4)
#define MEC1322_PCR_EC_SLP_EN    REG32(MEC1322_PCR_BASE + 0x8)
/* Command all blocks to sleep */
#define  MEC1322_PCR_EC_SLP_EN_SLEEP	0xe0700ff7
/* Allow all blocks to request clocks */
#define  MEC1322_PCR_EC_SLP_EN_WAKE	(~0xe0700ff7)
#define MEC1322_PCR_EC_CLK_REQ   REG32(MEC1322_PCR_BASE + 0xc)
#define MEC1322_PCR_HOST_SLP_EN  REG32(MEC1322_PCR_BASE + 0x10)
/* Command all blocks to sleep */
#define  MEC1322_PCR_HOST_SLP_EN_SLEEP	0x5f003
/* Allow all blocks to request clocks */
#define  MEC1322_PCR_HOST_SLP_EN_WAKE	(~0x5f003)
#define MEC1322_PCR_HOST_CLK_REQ REG32(MEC1322_PCR_BASE + 0x14)
#define MEC1322_PCR_SYS_SLP_CTL  REG32(MEC1322_PCR_BASE + 0x18)
#define MEC1322_PCR_PROC_CLK_CTL REG32(MEC1322_PCR_BASE + 0x20)
#define MEC1322_PCR_EC_SLP_EN2   REG32(MEC1322_PCR_BASE + 0x24)
/* Mask to command all blocks to sleep */
#define  MEC1322_PCR_EC_SLP_EN2_SLEEP	0x1ffffff8
/* Allow all blocks to request clocks */
#define  MEC1322_PCR_EC_SLP_EN2_WAKE	(~0x03fffff8)
#define MEC1322_PCR_EC_CLK_REQ2  REG32(MEC1322_PCR_BASE + 0x28)
#define MEC1322_PCR_SLOW_CLK_CTL REG32(MEC1322_PCR_BASE + 0x2c)
#define MEC1322_PCR_CHIP_OSC_ID  REG32(MEC1322_PCR_BASE + 0x30)
#define MEC1322_PCR_CHIP_PWR_RST REG32(MEC1322_PCR_BASE + 0x34)
#define MEC1322_PCR_CHIP_RST_EN  REG32(MEC1322_PCR_BASE + 0x38)
#define MEC1322_PCR_HOST_RST_EN  REG32(MEC1322_PCR_BASE + 0x3c)
#define MEC1322_PCR_EC_RST_EN    REG32(MEC1322_PCR_BASE + 0x40)
#define MEC1322_PCR_EC_RST_EN2   REG32(MEC1322_PCR_BASE + 0x44)
#define MEC1322_PCR_PWR_RST_CTL  REG32(MEC1322_PCR_BASE + 0x48)

/* Bit defines for MEC1322_PCR_CHIP_PWR_RST */
#define MEC1322_PWR_RST_STS_VCC1 (1 << 6)
#define MEC1322_PWR_RST_STS_VBAT (1 << 5)

/* EC Subsystem */
#define MEC1322_EC_BASE        0x4000fc00
#define MEC1322_EC_INT_CTRL    REG32(MEC1322_EC_BASE + 0x18)
#define MEC1322_EC_TRACE_EN    REG32(MEC1322_EC_BASE + 0x1c)
#define MEC1322_EC_JTAG_EN     REG32(MEC1322_EC_BASE + 0x20)
#define MEC1322_EC_WDT_CNT     REG32(MEC1322_EC_BASE + 0x28)
#define MEC1322_EC_ADC_VREF_PD REG32(MEC1322_EC_BASE + 0x38)

/* Interrupt aggregator */
#define MEC1322_INT_BASE       0x4000c000
#define MEC1322_INTx_BASE(x)   (MEC1322_INT_BASE + ((x) - 8) * 0x14)
#define MEC1322_INT_SOURCE(x)  REG32(MEC1322_INTx_BASE(x) + 0x0)
#define MEC1322_INT_ENABLE(x)  REG32(MEC1322_INTx_BASE(x) + 0x4)
#define MEC1322_INT_RESULT(x)  REG32(MEC1322_INTx_BASE(x) + 0x8)
#define MEC1322_INT_DISABLE(x) REG32(MEC1322_INTx_BASE(x) + 0xc)
#define MEC1322_INT_BLK_EN     REG32(MEC1322_INT_BASE + 0x200)
#define MEC1322_INT_BLK_DIS    REG32(MEC1322_INT_BASE + 0x204)
#define MEC1322_INT_BLK_IRQ    REG32(MEC1322_INT_BASE + 0x208)


/* UART */
#define MEC1322_UART_CONFIG_BASE  0x400f1f00
#define MEC1322_UART_RUNTIME_BASE 0x400f1c00

#define MEC1322_UART_ACT       REG8(MEC1322_UART_CONFIG_BASE + 0x30)
#define MEC1322_UART_CFG       REG8(MEC1322_UART_CONFIG_BASE + 0xf0)

/* DLAB=0 */
#define MEC1322_UART_RB /*R*/  REG8(MEC1322_UART_RUNTIME_BASE + 0x0)
#define MEC1322_UART_TB /*W*/  REG8(MEC1322_UART_RUNTIME_BASE + 0x0)
#define MEC1322_UART_IER       REG8(MEC1322_UART_RUNTIME_BASE + 0x1)
/* DLAB=1 */
#define MEC1322_UART_PBRG0     REG8(MEC1322_UART_RUNTIME_BASE + 0x0)
#define MEC1322_UART_PBRG1     REG8(MEC1322_UART_RUNTIME_BASE + 0x1)

#define MEC1322_UART_FCR /*W*/ REG8(MEC1322_UART_RUNTIME_BASE + 0x2)
#define MEC1322_UART_IIR /*R*/ REG8(MEC1322_UART_RUNTIME_BASE + 0x2)
#define MEC1322_UART_LCR       REG8(MEC1322_UART_RUNTIME_BASE + 0x3)
#define MEC1322_UART_MCR       REG8(MEC1322_UART_RUNTIME_BASE + 0x4)
#define MEC1322_UART_LSR       REG8(MEC1322_UART_RUNTIME_BASE + 0x5)
#define MEC1322_UART_MSR       REG8(MEC1322_UART_RUNTIME_BASE + 0x6)
#define MEC1322_UART_SCR       REG8(MEC1322_UART_RUNTIME_BASE + 0x7)

/* Bit defines for MEC1322_UART_LSR */
#define MEC1322_LSR_TX_EMPTY     (1 << 5)

/* GPIO */
#define MEC1322_GPIO_BASE      0x40081000

static inline uintptr_t gpio_port_base(int port_id)
{
	int oct = (port_id / 10) * 8 + port_id % 10;
	return MEC1322_GPIO_BASE + oct * 0x20;
}
#define MEC1322_GPIO_CTL(port, id) REG32(gpio_port_base(port) + (id << 2))

#define DUMMY_GPIO_BANK 0


/* Timer */
#define MEC1322_TMR16_BASE(x)  (0x40000c00 + (x) * 0x20)
#define MEC1322_TMR32_BASE(x)  (0x40000c80 + (x) * 0x20)

#define MEC1322_TMR16_CNT(x)   REG32(MEC1322_TMR16_BASE(x) + 0x0)
#define MEC1322_TMR16_PRE(x)   REG32(MEC1322_TMR16_BASE(x) + 0x4)
#define MEC1322_TMR16_STS(x)   REG32(MEC1322_TMR16_BASE(x) + 0x8)
#define MEC1322_TMR16_IEN(x)   REG32(MEC1322_TMR16_BASE(x) + 0xc)
#define MEC1322_TMR16_CTL(x)   REG32(MEC1322_TMR16_BASE(x) + 0x10)
#define MEC1322_TMR32_CNT(x)   REG32(MEC1322_TMR32_BASE(x) + 0x0)
#define MEC1322_TMR32_PRE(x)   REG32(MEC1322_TMR32_BASE(x) + 0x4)
#define MEC1322_TMR32_STS(x)   REG32(MEC1322_TMR32_BASE(x) + 0x8)
#define MEC1322_TMR32_IEN(x)   REG32(MEC1322_TMR32_BASE(x) + 0xc)
#define MEC1322_TMR32_CTL(x)   REG32(MEC1322_TMR32_BASE(x) + 0x10)


/* Watchdog */
#define MEC1322_WDG_BASE       0x40000400
#define MEC1322_WDG_LOAD       REG16(MEC1322_WDG_BASE + 0x0)
#define MEC1322_WDG_CTL        REG8(MEC1322_WDG_BASE + 0x4)
#define MEC1322_WDG_KICK       REG8(MEC1322_WDG_BASE + 0x8)
#define MEC1322_WDG_CNT        REG16(MEC1322_WDG_BASE + 0xc)


/* VBAT */
#define MEC1322_VBAT_BASE      0x4000a400
#define MEC1322_VBAT_STS       REG32(MEC1322_VBAT_BASE + 0x0)
#define MEC1322_VBAT_CE        REG32(MEC1322_VBAT_BASE + 0x8)
#define MEC1322_VBAT_RAM(x)    REG32(MEC1322_VBAT_BASE + 0x400 + 4 * (x))

/* Bit definition for MEC1322_VBAT_STS */
#define MEC1322_VBAT_STS_WDT	(1 << 5)

/* Miscellaneous firmware control fields
 * scratch pad index cannot be more than 16 as
 * mec has 64 bytes = 16 indexes of scratchpad RAM
 */
#define MEC1322_IMAGETYPE_IDX     15

/* LPC */
#define MEC1322_LPC_CFG_BASE     0x400f3300
#define MEC1322_LPC_ACT          REG8(MEC1322_LPC_CFG_BASE + 0x30)
#define MEC1322_LPC_SIRQ(x)      REG8(MEC1322_LPC_CFG_BASE + 0x40 + (x))
#define MEC1322_LPC_CFG_BAR      REG32(MEC1322_LPC_CFG_BASE + 0x60)
#define MEC1322_LPC_EMI_BAR      REG32(MEC1322_LPC_CFG_BASE + 0x64)
#define MEC1322_LPC_UART_BAR     REG32(MEC1322_LPC_CFG_BASE + 0x68)
#define MEC1322_LPC_8042_BAR     REG32(MEC1322_LPC_CFG_BASE + 0x78)
#define MEC1322_LPC_ACPI_EC0_BAR REG32(MEC1322_LPC_CFG_BASE + 0x88)
#define MEC1322_LPC_ACPI_EC1_BAR REG32(MEC1322_LPC_CFG_BASE + 0x8c)
#define MEC1322_LPC_ACPI_PM1_BAR REG32(MEC1322_LPC_CFG_BASE + 0x90)
#define MEC1322_LPC_PORT92_BAR   REG32(MEC1322_LPC_CFG_BASE + 0x94)
#define MEC1322_LPC_MAILBOX_BAR  REG32(MEC1322_LPC_CFG_BASE + 0x98)
#define MEC1322_LPC_RTC_BAR      REG32(MEC1322_LPC_CFG_BASE + 0x9c)
#define MEC1322_LPC_MEM_BAR      REG32(MEC1322_LPC_CFG_BASE + 0xa0)
#define MEC1322_LPC_MEM_BAR_CFG  REG32(MEC1322_LPC_CFG_BASE + 0xa4)

#define MEC1322_LPC_RT_BASE      0x400f3100
#define MEC1322_LPC_BUS_MONITOR  REG32(MEC1322_LPC_RT_BASE + 0x4)
#define MEC1322_LPC_CLK_CTRL     REG32(MEC1322_LPC_RT_BASE + 0x10)
#define MEC1322_LPC_MEM_HOST_CFG REG32(MEC1322_LPC_RT_BASE + 0xfc)


/* EMI */
#define MEC1322_EMI_BASE       0x400f0100
#define MEC1322_EMI_H2E_MBX    REG8(MEC1322_EMI_BASE + 0x0)
#define MEC1322_EMI_E2H_MBX    REG8(MEC1322_EMI_BASE + 0x1)
#define MEC1322_EMI_MBA0       REG32(MEC1322_EMI_BASE + 0x4)
#define MEC1322_EMI_MRL0       REG16(MEC1322_EMI_BASE + 0x8)
#define MEC1322_EMI_MWL0       REG16(MEC1322_EMI_BASE + 0xa)
#define MEC1322_EMI_MBA1       REG32(MEC1322_EMI_BASE + 0xc)
#define MEC1322_EMI_MRL1       REG16(MEC1322_EMI_BASE + 0x10)
#define MEC1322_EMI_MWL1       REG16(MEC1322_EMI_BASE + 0x12)
#define MEC1322_EMI_ISR        REG16(MEC1322_EMI_BASE + 0x14)
#define MEC1322_EMI_HCE        REG16(MEC1322_EMI_BASE + 0x16)

#define MEC1322_EMI_RT_BASE    0x400f0000
#define MEC1322_EMI_ISR_B0     REG8(MEC1322_EMI_RT_BASE + 0x8)
#define MEC1322_EMI_ISR_B1     REG8(MEC1322_EMI_RT_BASE + 0x9)
#define MEC1322_EMI_IMR_B0     REG8(MEC1322_EMI_RT_BASE + 0xa)
#define MEC1322_EMI_IMR_B1     REG8(MEC1322_EMI_RT_BASE + 0xb)


/* Mailbox */
#define MEC1322_MBX_RT_BASE    0x400f2400
#define MEC1322_MBX_INDEX      REG8(MEC1322_MBX_RT_BASE + 0x0)
#define MEC1322_MBX_DATA       REG8(MEC1322_MBX_RT_BASE + 0x1)

#define MEC1322_MBX_BASE       0x400f2500
#define MEC1322_MBX_H2E_MBX    REG8(MEC1322_MBX_BASE + 0x0)
#define MEC1322_MBX_E2H_MBX    REG8(MEC1322_MBX_BASE + 0x4)
#define MEC1322_MBX_ISR        REG8(MEC1322_MBX_BASE + 0x8)
#define MEC1322_MBX_IMR        REG8(MEC1322_MBX_BASE + 0xc)
#define MEC1322_MBX_REG(x)     REG8(MEC1322_MBX_BASE + 0x10 + (x))


/* PWM */
#define MEC1322_PWM_BASE(x)    (0x40005800 + (x) * 0x10)
#define MEC1322_PWM_ON(x)      REG32(MEC1322_PWM_BASE(x) + 0x00)
#define MEC1322_PWM_OFF(x)     REG32(MEC1322_PWM_BASE(x) + 0x04)
#define MEC1322_PWM_CFG(x)     REG32(MEC1322_PWM_BASE(x) + 0x08)


/* ACPI */
#define MEC1322_ACPI_EC_BASE(x)     (0x400f0c00 + (x) * 0x400)
#define MEC1322_ACPI_EC_EC2OS(x, y) REG8(MEC1322_ACPI_EC_BASE(x) + 0x100 + (y))
#define MEC1322_ACPI_EC_STATUS(x)   REG8(MEC1322_ACPI_EC_BASE(x) + 0x104)
#define MEC1322_ACPI_EC_BYTE_CTL(x) REG8(MEC1322_ACPI_EC_BASE(x) + 0x105)
#define MEC1322_ACPI_EC_OS2EC(x, y) REG8(MEC1322_ACPI_EC_BASE(x) + 0x108 + (y))

#define MEC1322_ACPI_PM_RT_BASE     0x400f1400
#define MEC1322_ACPI_PM1_STS1       REG8(MEC1322_ACPI_PM_RT_BASE + 0x0)
#define MEC1322_ACPI_PM1_STS2       REG8(MEC1322_ACPI_PM_RT_BASE + 0x1)
#define MEC1322_ACPI_PM1_EN1        REG8(MEC1322_ACPI_PM_RT_BASE + 0x2)
#define MEC1322_ACPI_PM1_EN2        REG8(MEC1322_ACPI_PM_RT_BASE + 0x3)
#define MEC1322_ACPI_PM1_CTL1       REG8(MEC1322_ACPI_PM_RT_BASE + 0x4)
#define MEC1322_ACPI_PM1_CTL2       REG8(MEC1322_ACPI_PM_RT_BASE + 0x5)
#define MEC1322_ACPI_PM2_CTL1       REG8(MEC1322_ACPI_PM_RT_BASE + 0x6)
#define MEC1322_ACPI_PM2_CTL2       REG8(MEC1322_ACPI_PM_RT_BASE + 0x7)
#define MEC1322_ACPI_PM_EC_BASE     0x400f1500
#define MEC1322_ACPI_PM_STS         REG8(MEC1322_ACPI_PM_EC_BASE + 0x10)


/* 8042 */
#define MEC1322_8042_BASE      0x400f0400
#define MEC1322_8042_OBF_CLR   REG8(MEC1322_8042_BASE + 0x0)
#define MEC1322_8042_H2E       REG8(MEC1322_8042_BASE + 0x100)
#define MEC1322_8042_E2H       REG8(MEC1322_8042_BASE + 0x100)
#define MEC1322_8042_STS       REG8(MEC1322_8042_BASE + 0x104)
#define MEC1322_8042_KB_CTRL   REG8(MEC1322_8042_BASE + 0x108)
#define MEC1322_8042_PCOBF     REG8(MEC1322_8042_BASE + 0x114)
#define MEC1322_8042_ACT       REG8(MEC1322_8042_BASE + 0x330)


/* FAN */
#define MEC1322_FAN_BASE       0x4000a000
#define MEC1322_FAN_SETTING    REG8(MEC1322_FAN_BASE + 0x0)
#define MEC1322_FAN_PWM_DIVIDE REG8(MEC1322_FAN_BASE + 0x1)
#define MEC1322_FAN_CFG1       REG8(MEC1322_FAN_BASE + 0x2)
#define MEC1322_FAN_CFG2       REG8(MEC1322_FAN_BASE + 0x3)
#define MEC1322_FAN_GAIN       REG8(MEC1322_FAN_BASE + 0x5)
#define MEC1322_FAN_SPIN_UP    REG8(MEC1322_FAN_BASE + 0x6)
#define MEC1322_FAN_STEP       REG8(MEC1322_FAN_BASE + 0x7)
#define MEC1322_FAN_MIN_DRV    REG8(MEC1322_FAN_BASE + 0x8)
#define MEC1322_FAN_VALID_CNT  REG8(MEC1322_FAN_BASE + 0x9)
#define MEC1322_FAN_DRV_FAIL   REG16(MEC1322_FAN_BASE + 0xa)
#define MEC1322_FAN_TARGET     REG16(MEC1322_FAN_BASE + 0xc)
#define MEC1322_FAN_READING    REG16(MEC1322_FAN_BASE + 0xe)
#define MEC1322_FAN_BASE_FREQ  REG8(MEC1322_FAN_BASE + 0x10)
#define MEC1322_FAN_STATUS     REG8(MEC1322_FAN_BASE + 0x11)


/* I2C */
#define MEC1322_I2C0_BASE      0x40001800
#define MEC1322_I2C1_BASE      0x4000ac00
#define MEC1322_I2C2_BASE      0x4000b000
#define MEC1322_I2C3_BASE      0x4000b400
#define MEC1322_I2C_BASESEP    0x00000400
#define MEC1322_I2C_ADDR(controller, offset) \
	(offset + (controller == 0 ? MEC1322_I2C0_BASE : \
		   MEC1322_I2C1_BASE + MEC1322_I2C_BASESEP * (controller - 1)))

/*
 * MEC1322 has five ports distributed among four controllers. Locking must
 * occur by-controller (not by-port).
 */
enum mec1322_i2c_port {
	MEC1322_I2C0_0 = 0,      /* Controller 0, port 0 */
	MEC1322_I2C0_1 = 1,      /* Controller 0, port 1 */
	MEC1322_I2C1   = 2,      /* Controller 1 */
	MEC1322_I2C2   = 3,      /* Controller 2 */
	MEC1322_I2C3   = 4,      /* Controller 3 */
	MEC1322_I2C_PORT_COUNT,
};

#define MEC1322_I2C_CTRL(ctrl)          REG8(MEC1322_I2C_ADDR(ctrl, 0x0))
#define MEC1322_I2C_STATUS(ctrl)        REG8(MEC1322_I2C_ADDR(ctrl, 0x0))
#define MEC1322_I2C_OWN_ADDR(ctrl)      REG16(MEC1322_I2C_ADDR(ctrl, 0x4))
#define MEC1322_I2C_DATA(ctrl)          REG8(MEC1322_I2C_ADDR(ctrl, 0x8))
#define MEC1322_I2C_MASTER_CMD(ctrl)    REG32(MEC1322_I2C_ADDR(ctrl, 0xc))
#define MEC1322_I2C_SLAVE_CMD(ctrl)     REG32(MEC1322_I2C_ADDR(ctrl, 0x10))
#define MEC1322_I2C_PEC(ctrl)           REG8(MEC1322_I2C_ADDR(ctrl, 0x14))
#define MEC1322_I2C_DATA_TIM_2(ctrl)    REG8(MEC1322_I2C_ADDR(ctrl, 0x18))
#define MEC1322_I2C_COMPLETE(ctrl)      REG32(MEC1322_I2C_ADDR(ctrl, 0x20))
#define MEC1322_I2C_IDLE_SCALE(ctrl)    REG32(MEC1322_I2C_ADDR(ctrl, 0x24))
#define MEC1322_I2C_CONFIG(ctrl)        REG32(MEC1322_I2C_ADDR(ctrl, 0x28))
#define MEC1322_I2C_BUS_CLK(ctrl)       REG16(MEC1322_I2C_ADDR(ctrl, 0x2c))
#define MEC1322_I2C_BLK_ID(ctrl)        REG8(MEC1322_I2C_ADDR(ctrl, 0x30))
#define MEC1322_I2C_REV(ctrl)           REG8(MEC1322_I2C_ADDR(ctrl, 0x34))
#define MEC1322_I2C_BB_CTRL(ctrl)       REG8(MEC1322_I2C_ADDR(ctrl, 0x38))
#define MEC1322_I2C_DATA_TIM(ctrl)      REG32(MEC1322_I2C_ADDR(ctrl, 0x40))
#define MEC1322_I2C_TOUT_SCALE(ctrl)    REG32(MEC1322_I2C_ADDR(ctrl, 0x44))
#define MEC1322_I2C_SLAVE_TX_BUF(ctrl)  REG8(MEC1322_I2C_ADDR(ctrl, 0x48))
#define MEC1322_I2C_SLAVE_RX_BUF(ctrl)  REG8(MEC1322_I2C_ADDR(ctrl, 0x4c))
#define MEC1322_I2C_MASTER_TX_BUF(ctrl) REG8(MEC1322_I2C_ADDR(ctrl, 0x50))
#define MEC1322_I2C_MASTER_RX_BUF(ctrl) REG8(MEC1322_I2C_ADDR(ctrl, 0x54))


/* Keyboard scan matrix */
#define MEC1322_KS_BASE        0x40009c00
#define MEC1322_KS_KSO_SEL     REG32(MEC1322_KS_BASE + 0x4)
#define MEC1322_KS_KSI_INPUT   REG32(MEC1322_KS_BASE + 0x8)
#define MEC1322_KS_KSI_STATUS  REG32(MEC1322_KS_BASE + 0xc)
#define MEC1322_KS_KSI_INT_EN  REG32(MEC1322_KS_BASE + 0x10)
#define MEC1322_KS_EXT_CTRL    REG32(MEC1322_KS_BASE + 0x14)


/* ADC */
#define MEC1322_ADC_BASE       0x40007c00
#define MEC1322_ADC_CTRL       REG32(MEC1322_ADC_BASE + 0x0)
#define MEC1322_ADC_DELAY      REG32(MEC1322_ADC_BASE + 0x4)
#define MEC1322_ADC_STS        REG32(MEC1322_ADC_BASE + 0x8)
#define MEC1322_ADC_SINGLE     REG32(MEC1322_ADC_BASE + 0xc)
#define MEC1322_ADC_REPEAT     REG32(MEC1322_ADC_BASE + 0x10)
#define MEC1322_ADC_READ(x)    REG32(MEC1322_ADC_BASE + 0x14 + (x) * 0x4)


/* Hibernation timer */
#define MEC1322_HTIMER_BASE    0x40009800
#define MEC1322_HTIMER_PRELOAD REG16(MEC1322_HTIMER_BASE + 0x0)
#define MEC1322_HTIMER_CONTROL REG16(MEC1322_HTIMER_BASE + 0x4)
#define MEC1322_HTIMER_COUNT   REG16(MEC1322_HTIMER_BASE + 0x8)


/* SPI */
#define MEC1322_SPI_BASE(port) (0x40009400 + 0x80 * (port))
#define MEC1322_SPI_AR(port)   REG8(MEC1322_SPI_BASE(port) + 0x00)
#define MEC1322_SPI_CR(port)   REG8(MEC1322_SPI_BASE(port) + 0x04)
#define MEC1322_SPI_SR(port)   REG8(MEC1322_SPI_BASE(port) + 0x08)
#define MEC1322_SPI_TD(port)   REG8(MEC1322_SPI_BASE(port) + 0x0c)
#define MEC1322_SPI_RD(port)   REG8(MEC1322_SPI_BASE(port) + 0x10)
#define MEC1322_SPI_CC(port)   REG8(MEC1322_SPI_BASE(port) + 0x14)
#define MEC1322_SPI_CG(port)   REG8(MEC1322_SPI_BASE(port) + 0x18)


/* DMA */
#define MEC1322_DMA_BASE            0x40002400

/*
 * Available DMA channels.
 *
 * On MEC1322, any DMA channel may serve any device. Since we have
 * 12 channels and 12 devices, we make each channel dedicated to the
 * device of the same number.
 */
enum dma_channel {
	/* Channel numbers */
	MEC1322_DMAC_I2C0_SLAVE =  0,
	MEC1322_DMAC_I2C0_MASTER = 1,
	MEC1322_DMAC_I2C1_SLAVE =  2,
	MEC1322_DMAC_I2C1_MASTER = 3,
	MEC1322_DMAC_I2C2_SLAVE =  4,
	MEC1322_DMAC_I2C2_MASTER = 5,
	MEC1322_DMAC_I2C3_SLAVE =  6,
	MEC1322_DMAC_I2C3_MASTER = 7,
	MEC1322_DMAC_SPI0_TX =     8,
	MEC1322_DMAC_SPI0_RX =     9,
	MEC1322_DMAC_SPI1_TX =    10,
	MEC1322_DMAC_SPI1_RX =    11,

	/* Channel count */
	MEC1322_DMAC_COUNT =      12,
};

/* Registers for a single channel of the DMA controller */
struct mec1322_dma_chan {
	uint32_t act;         /* Activate */
	uint32_t mem_start;   /* Memory start address */
	uint32_t mem_end;     /* Memory end address */
	uint32_t dev;         /* Device address */
	uint32_t ctrl;        /* Control */
	uint32_t int_status;  /* Interrupt status */
	uint32_t int_enabled; /* Interrupt enabled */
	uint32_t pad;
};

/* Always use mec1322_dma_chan_t so volatile keyword is included! */
typedef volatile struct mec1322_dma_chan mec1322_dma_chan_t;

/* Common code and header file must use this */
typedef mec1322_dma_chan_t dma_chan_t;

/* Registers for the DMA controller */
struct mec1322_dma_regs {
	uint32_t ctrl;
	uint32_t data;
	uint32_t pad[2];
	mec1322_dma_chan_t chan[MEC1322_DMAC_COUNT];
};

/* Always use mec1322_dma_regs_t so volatile keyword is included! */
typedef volatile struct mec1322_dma_regs mec1322_dma_regs_t;

#define MEC1322_DMA_REGS ((mec1322_dma_regs_t *)MEC1322_DMA_BASE)

/* Bits for DMA channel regs */
#define MEC1322_DMA_ACT_EN		(1 << 0)
#define MEC1322_DMA_XFER_SIZE(x)	((x) << 20)
#define MEC1322_DMA_INC_DEV		(1 << 17)
#define MEC1322_DMA_INC_MEM		(1 << 16)
#define MEC1322_DMA_DEV(x)		((x) << 9)
#define MEC1322_DMA_TO_DEV		(1 << 8)
#define MEC1322_DMA_DONE		(1 << 2)
#define MEC1322_DMA_RUN			(1 << 0)


/* IRQ Numbers */
#define MEC1322_IRQ_I2C_0        0
#define MEC1322_IRQ_I2C_1        1
#define MEC1322_IRQ_I2C_2        2
#define MEC1322_IRQ_I2C_3        3
#define MEC1322_IRQ_DMA_0        4
#define MEC1322_IRQ_DMA_1        5
#define MEC1322_IRQ_DMA_2        6
#define MEC1322_IRQ_DMA_3        7
#define MEC1322_IRQ_DMA_4        8
#define MEC1322_IRQ_DMA_5        9
#define MEC1322_IRQ_DMA_6        10
#define MEC1322_IRQ_DMA_7        11
#define MEC1322_IRQ_LPC          12
#define MEC1322_IRQ_UART         13
#define MEC1322_IRQ_EMI          14
#define MEC1322_IRQ_ACPIEC0_IBF  15
#define MEC1322_IRQ_ACPIEC0_OBF  16
#define MEC1322_IRQ_ACPIEC1_IBF  17
#define MEC1322_IRQ_ACPIEC1_OBF  18
#define MEC1322_IRQ_ACPIPM1_CTL  19
#define MEC1322_IRQ_ACPIPM1_EN   20
#define MEC1322_IRQ_ACPIPM1_STS  21
#define MEC1322_IRQ_8042EM_OBF   22
#define MEC1322_IRQ_8042EM_IBF   23
#define MEC1322_IRQ_MAILBOX      24
#define MEC1322_IRQ_PECI_HOST    25
#define MEC1322_IRQ_TACH_0       26
#define MEC1322_IRQ_TACH_1       27
#define MEC1322_IRQ_ADC_SNGL     28
#define MEC1322_IRQ_ADC_RPT      29
#define MEC1322_IRQ_PS2_0        32
#define MEC1322_IRQ_PS2_1        33
#define MEC1322_IRQ_PS2_2        34
#define MEC1322_IRQ_PS2_3        35
#define MEC1322_IRQ_SPI0_TX      36
#define MEC1322_IRQ_SPI0_RX      37
#define MEC1322_IRQ_HTIMER       38
#define MEC1322_IRQ_KSC_INT      39
#define MEC1322_IRQ_MAILBOX_DATA 40
#define MEC1322_IRQ_TIMER16_0    49
#define MEC1322_IRQ_TIMER16_1    50
#define MEC1322_IRQ_TIMER16_2    51
#define MEC1322_IRQ_TIMER16_3    52
#define MEC1322_IRQ_TIMER32_0    53
#define MEC1322_IRQ_TIMER32_1    54
#define MEC1322_IRQ_SPI1_TX      55
#define MEC1322_IRQ_SPI1_RX      56
#define MEC1322_IRQ_GIRQ8        57
#define MEC1322_IRQ_GIRQ9        58
#define MEC1322_IRQ_GIRQ10       59
#define MEC1322_IRQ_GIRQ11       60
#define MEC1322_IRQ_GIRQ12       61
#define MEC1322_IRQ_GIRQ13       62
#define MEC1322_IRQ_GIRQ14       63
#define MEC1322_IRQ_GIRQ15       64
#define MEC1322_IRQ_GIRQ16       65
#define MEC1322_IRQ_GIRQ17       66
#define MEC1322_IRQ_GIRQ18       67
#define MEC1322_IRQ_GIRQ19       68
#define MEC1322_IRQ_GIRQ20       69
#define MEC1322_IRQ_GIRQ21       70
#define MEC1322_IRQ_GIRQ22       71
#define MEC1322_IRQ_GIRQ23       72
#define MEC1322_IRQ_DMA_8        81
#define MEC1322_IRQ_DMA_9        82
#define MEC1322_IRQ_DMA_10       83
#define MEC1322_IRQ_DMA_11       84
#define MEC1322_IRQ_PWM_WDT3     85
#define MEC1322_IRQ_RTC          91
#define MEC1322_IRQ_RTC_ALARM    92

/* Wake pin definitions, defined at board-level */
extern const enum gpio_signal hibernate_wake_pins[];
extern const int hibernate_wake_pins_used;

#endif /* __CROS_EC_REGISTERS_H */
