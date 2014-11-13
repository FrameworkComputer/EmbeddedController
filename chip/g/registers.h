/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"


/* Replace masked bits with val << lsb */
#define REG_WRITE_MASK(reg, mask, val, lsb) \
	reg = ((reg & ~mask) | ((val << lsb) & mask))

/* Revision */
#define G_REVISION_STR "A1"

#define G_PINMUX_BASE_ADDR                0x40060000
#define G_PINMUX_DIOA0_SEL                REG32(G_PINMUX_BASE_ADDR + 0x0028)
#define G_PINMUX_DIOA0_CTL                REG32(G_PINMUX_BASE_ADDR + 0x002c)
#define G_PINMUX_DIOA1_CTL                REG32(G_PINMUX_BASE_ADDR + 0x0034)
#define G_PINMUX_UART0_RX_SEL             REG32(G_PINMUX_BASE_ADDR + 0x02a8)

#define G_PINMUX_DIOA0_CTL_IE_LSB                 0x2
#define G_PINMUX_DIOA0_CTL_IE_MASK                0x4
#define G_PINMUX_DIOA1_CTL_IE_LSB                 0x2
#define G_PINMUX_DIOA1_CTL_IE_MASK                0x4
#define G_PINMUX_DIOA1_SEL                        0x7
#define G_PINMUX_UART0_TX_SEL                     0x40


#define G_PMU_BASE_ADDR                   0x40000000
#define G_PMU_CLRDIS                      REG32(G_PMU_BASE_ADDR + 0x0018)
#define G_PMU_OSC_HOLD_SET                REG32(G_PMU_BASE_ADDR + 0x0080)
#define G_PMU_OSC_HOLD_CLR                REG32(G_PMU_BASE_ADDR + 0x0084)
#define G_PMU_OSC_SELECT                  REG32(G_PMU_BASE_ADDR + 0x0088)
#define G_PMU_OSC_SELECT_STAT             REG32(G_PMU_BASE_ADDR + 0x008c)
#define G_PMU_OSC_CTRL                    REG32(G_PMU_BASE_ADDR + 0x0090)
#define G_PMU_PERICLKSET0                 REG32(G_PMU_BASE_ADDR + 0x009c)
#define G_PMU_FUSE_RD_RC_OSC_26MHZ        REG32(G_PMU_BASE_ADDR + 0x011c)
#define G_PMU_FUSE_RD_XTL_OSC_26MHZ       REG32(G_PMU_BASE_ADDR + 0x0124)

#define G_PMU_FUSE_RD_RC_OSC_26MHZ_EN_MASK        0x10000000
#define G_PMU_FUSE_RD_RC_OSC_26MHZ_TRIM_LSB       0x0
#define G_PMU_FUSE_RD_RC_OSC_26MHZ_TRIM_MASK      0xfffffff
#define G_PMU_FUSE_RD_XTL_OSC_26MHZ_EN_MASK       0x10
#define G_PMU_FUSE_RD_XTL_OSC_26MHZ_TRIM_LSB      0x0
#define G_PMU_FUSE_RD_XTL_OSC_26MHZ_TRIM_MASK     0xf
#define G_PMU_OSC_CTRL_RC_TRIM_READYB_LSB         0x1
#define G_PMU_OSC_CTRL_RC_TRIM_READYB_MASK        0x2
#define G_PMU_OSC_CTRL_XTL_READYB_LSB             0x0
#define G_PMU_OSC_CTRL_XTL_READYB_MASK            0x1
#define G_PMU_OSC_SELECT_RC                       0x3
#define G_PMU_OSC_SELECT_RC_TRIM                  0x2
#define G_PMU_OSC_SELECT_XTL                      0x0
#define G_PMU_PERICLKSET0_DXO0_LSB                0x18
#define G_PMU_PERICLKSET0_DUART0_LSB              0x14
#define G_PMU_SETDIS_RC_TRIM_LSB                  0xf
#define G_PMU_SETDIS_XTL_LSB                      0xe


/* More than one UART */
#define G_UART0_BASE_ADDR                         0x40540000
#define G_UART1_BASE_ADDR                         0x40550000
#define G_UART2_BASE_ADDR                         0x40560000
#define G_UART_BASE_ADDR_SEP                      0x00010000
static inline int g_uart_addr(int ch, int offset)
{
	return offset + G_UART0_BASE_ADDR + G_UART_BASE_ADDR_SEP * ch;
}
#define G_UARTREG(ch, offset)                     REG32(g_uart_addr(ch, offset))
#define G_UART_RDATA(ch)                          G_UARTREG(ch, 0x0000)
#define G_UART_WDATA(ch)                          G_UARTREG(ch, 0x0004)
#define G_UART_NCO(ch)                            G_UARTREG(ch, 0x0008)
#define G_UART_CTRL(ch)                           G_UARTREG(ch, 0x000c)
#define G_UART_ICTRL(ch)                          G_UARTREG(ch, 0x0010)
#define G_UART_STATE(ch)                          G_UARTREG(ch, 0x0014)
#define G_UART_ISTATECLR(ch)                      G_UARTREG(ch, 0x0020)
#define G_UART_FIFO(ch)                           G_UARTREG(ch, 0x0024)
#define G_UART_RFIFO(ch)                          G_UARTREG(ch, 0x0028)


/*
 * High-speed timers. Two modules with two timers each; four timers total.
 */
#define G_TIMEHS0_BASE_ADDR                       0x40570000
#define G_TIMEHS1_BASE_ADDR                       0x40580000
#define G_TIMEHS_BASE_ADDR_SEP                    0x00010000
#define G_TIMEHSX_TIMER1_OFS                            0x00
#define G_TIMEHSX_TIMER2_OFS                            0x20
#define G_TIMEHSX_TIMER_OFS_SEP                         0x20
/* NOTE: module is 0-1, timer is 1-2 */
static inline int g_timehs_addr(unsigned int module, unsigned int timer,
				int offset)
{
	return G_TIMEHS0_BASE_ADDR + G_TIMEHS_BASE_ADDR_SEP * module
		+ G_TIMEHSX_TIMER1_OFS + G_TIMEHSX_TIMER_OFS_SEP * (timer - 1)
		+ offset;
}
/* Per-timer registers */
#define G_TIMEHSREG(m, t, ofs)            REG32(g_timehs_addr(m, t, ofs))
#define G_TIMEHS_LOAD(m, t)               G_TIMEHSREG(m, t, 0x0000)
#define G_TIMEHS_VALUE(m, t)              G_TIMEHSREG(m, t, 0x0004)
#define G_TIMEHS_CONTROL(m, t)            G_TIMEHSREG(m, t, 0x0008)
#define G_TIMEHS_INTCLR(m, t)             G_TIMEHSREG(m, t, 0x000c)
#define G_TIMEHS_RIS(m, t)                G_TIMEHSREG(m, t, 0x0010)
#define G_TIMEHS_MIS(m, t)                G_TIMEHSREG(m, t, 0x0014)
#define G_TIMEHS_BGLOAD(m, t)             G_TIMEHSREG(m, t, 0x0018)
/* These are only per-module */
#define G_TIMEHS_ITCR(m)                  G_TIMEHSREG(m, 1, 0x0f00)
#define G_TIMEHS_ITOP(m)                  G_TIMEHSREG(m, 1, 0x0f04)
#define G_TIMEHS_PERIPHID4(m)             G_TIMEHSREG(m, 1, 0x0fd0)
#define G_TIMEHS_PERIPHID5(m)             G_TIMEHSREG(m, 1, 0x0fd4)
#define G_TIMEHS_PERIPHID6(m)             G_TIMEHSREG(m, 1, 0x0fd8)
#define G_TIMEHS_PERIPHID7(m)             G_TIMEHSREG(m, 1, 0x0fdc)
#define G_TIMEHS_PERIPHID0(m)             G_TIMEHSREG(m, 1, 0x0fe0)
#define G_TIMEHS_PERIPHID1(m)             G_TIMEHSREG(m, 1, 0x0fe4)
#define G_TIMEHS_PERIPHID2(m)             G_TIMEHSREG(m, 1, 0x0fe8)
#define G_TIMEHS_PERIPHID3(m)             G_TIMEHSREG(m, 1, 0x0fec)
#define G_TIMEHS_PCELLID0(m)              G_TIMEHSREG(m, 1, 0x0ff0)
#define G_TIMEHS_PCELLID1(m)              G_TIMEHSREG(m, 1, 0x0ff4)
#define G_TIMEHS_PCELLID2(m)              G_TIMEHSREG(m, 1, 0x0ff8)
#define G_TIMEHS_PCELLID3(m)              G_TIMEHSREG(m, 1, 0x0ffc)


/* Oscillator */
#define G_XO0_BASE_ADDR                   0x40420000
#define G_XO_OSC_RC_CAL_RSTB              REG32(G_XO0_BASE_ADDR + 0x0014)
#define G_XO_OSC_RC_CAL_LOAD              REG32(G_XO0_BASE_ADDR + 0x0018)
#define G_XO_OSC_RC_CAL_START             REG32(G_XO0_BASE_ADDR + 0x001c)
#define G_XO_OSC_RC_CAL_DONE              REG32(G_XO0_BASE_ADDR + 0x0020)
#define G_XO_OSC_RC_CAL_COUNT             REG32(G_XO0_BASE_ADDR + 0x0024)
#define G_XO_OSC_RC                       REG32(G_XO0_BASE_ADDR + 0x0028)
#define G_XO_OSC_RC_STATUS                REG32(G_XO0_BASE_ADDR + 0x002c)
#define G_XO_OSC_XTL_TRIM                 REG32(G_XO0_BASE_ADDR + 0x0048)
#define G_XO_OSC_XTL_TRIM_STAT            REG32(G_XO0_BASE_ADDR + 0x004c)
#define G_XO_OSC_XTL_FSM_EN               REG32(G_XO0_BASE_ADDR + 0x0050)
#define G_XO_OSC_XTL_FSM                  REG32(G_XO0_BASE_ADDR + 0x0054)
#define G_XO_OSC_XTL_FSM_CFG              REG32(G_XO0_BASE_ADDR + 0x0058)
#define G_XO_OSC_SETHOLD                  REG32(G_XO0_BASE_ADDR + 0x005c)
#define G_XO_OSC_CLRHOLD                  REG32(G_XO0_BASE_ADDR + 0x0060)

#define G_XO_OSC_CLRHOLD_RC_TRIM_LSB              0x0
#define G_XO_OSC_CLRHOLD_RC_TRIM_MASK             0x1
#define G_XO_OSC_CLRHOLD_XTL_LSB                  0x1
#define G_XO_OSC_RC_EN_LSB                        0x1c
#define G_XO_OSC_RC_STATUS_EN_MASK                0x10000000
#define G_XO_OSC_RC_STATUS_TRIM_LSB               0x0
#define G_XO_OSC_RC_STATUS_TRIM_MASK              0xfffffff
#define G_XO_OSC_RC_TRIM_LSB                      0x0
#define G_XO_OSC_RC_TRIM_MASK                     0xfffffff
#define G_XO_OSC_SETHOLD_RC_TRIM_LSB              0x0
#define G_XO_OSC_SETHOLD_RC_TRIM_MASK             0x1
#define G_XO_OSC_SETHOLD_XTL_LSB                  0x1
#define G_XO_OSC_XTL_FSM_CFG_TRIM_MAX_LSB         0x0
#define G_XO_OSC_XTL_FSM_CFG_TRIM_MAX_MASK        0xf
#define G_XO_OSC_XTL_FSM_DONE_MASK                0x1
#define G_XO_OSC_XTL_FSM_EN_KEY                   0x60221413
#define G_XO_OSC_XTL_FSM_STATUS_LSB               0x5
#define G_XO_OSC_XTL_FSM_STATUS_MASK              0x20
#define G_XO_OSC_XTL_FSM_TRIM_LSB                 0x1
#define G_XO_OSC_XTL_FSM_TRIM_MASK                0x1e
#define G_XO_OSC_XTL_TRIM_CODE_LSB                0x0
#define G_XO_OSC_XTL_TRIM_EN_LSB                  0x4
#define G_XO_OSC_XTL_TRIM_STAT_EN_MASK            0x10


/* Interrupts */
#define G_IRQNUM_CAMO0_BREACH_INT                 0
#define G_IRQNUM_FLASH0_EDONEINT                  1
#define G_IRQNUM_FLASH0_PDONEINT                  2
#define G_IRQNUM_GPIO0_GPIOCOMBINT                3
#define G_IRQNUM_GPIO0_GPIO0INT                   4
#define G_IRQNUM_GPIO0_GPIO1INT                   5
#define G_IRQNUM_GPIO0_GPIO2INT                   6
#define G_IRQNUM_GPIO0_GPIO3INT                   7
#define G_IRQNUM_GPIO0_GPIO4INT                   8
#define G_IRQNUM_GPIO0_GPIO5INT                   9
#define G_IRQNUM_GPIO0_GPIO6INT                   10
#define G_IRQNUM_GPIO0_GPIO7INT                   11
#define G_IRQNUM_GPIO0_GPIO8INT                   12
#define G_IRQNUM_GPIO0_GPIO9INT                   13
#define G_IRQNUM_GPIO0_GPIO10INT                  14
#define G_IRQNUM_GPIO0_GPIO11INT                  15
#define G_IRQNUM_GPIO0_GPIO12INT                  16
#define G_IRQNUM_GPIO0_GPIO13INT                  17
#define G_IRQNUM_GPIO0_GPIO14INT                  18
#define G_IRQNUM_GPIO0_GPIO15INT                  19
#define G_IRQNUM_GPIO1_GPIOCOMBINT                20
#define G_IRQNUM_GPIO1_GPIO0INT                   21
#define G_IRQNUM_GPIO1_GPIO1INT                   22
#define G_IRQNUM_GPIO1_GPIO2INT                   23
#define G_IRQNUM_GPIO1_GPIO3INT                   24
#define G_IRQNUM_GPIO1_GPIO4INT                   25
#define G_IRQNUM_GPIO1_GPIO5INT                   26
#define G_IRQNUM_GPIO1_GPIO6INT                   27
#define G_IRQNUM_GPIO1_GPIO7INT                   28
#define G_IRQNUM_GPIO1_GPIO8INT                   29
#define G_IRQNUM_GPIO1_GPIO9INT                   30
#define G_IRQNUM_GPIO1_GPIO10INT                  31
#define G_IRQNUM_GPIO1_GPIO11INT                  32
#define G_IRQNUM_GPIO1_GPIO12INT                  33
#define G_IRQNUM_GPIO1_GPIO13INT                  34
#define G_IRQNUM_GPIO1_GPIO14INT                  35
#define G_IRQNUM_GPIO1_GPIO15INT                  36
#define G_IRQNUM_I2C0_I2CINT                      37
#define G_IRQNUM_I2C1_I2CINT                      38
#define G_IRQNUM_PMU_PMUINT                       39
#define G_IRQNUM_SHA0_DSHA_INT                    40
#define G_IRQNUM_SPI0_SPITXINT                    41
#define G_IRQNUM_SPS0_CS_ASSERT_INTR              42
#define G_IRQNUM_SPS0_CS_DEASSERT_INTR            43
#define G_IRQNUM_SPS0_REGION0_BUF_LVL             44
#define G_IRQNUM_SPS0_REGION1_BUF_LVL             45
#define G_IRQNUM_SPS0_REGION2_BUF_LVL             46
#define G_IRQNUM_SPS0_REGION3_BUF_LVL             47
#define G_IRQNUM_SPS0_ROM_CMD_END                 48
#define G_IRQNUM_SPS0_ROM_CMD_START               49
#define G_IRQNUM_SPS0_RXFIFO_LVL_INTR             50
#define G_IRQNUM_SPS0_RXFIFO_OVERFLOW_INTR        51
#define G_IRQNUM_SPS0_SPSCTRLINT0                 52
#define G_IRQNUM_SPS0_SPSCTRLINT1                 53
#define G_IRQNUM_SPS0_SPSCTRLINT2                 54
#define G_IRQNUM_SPS0_SPSCTRLINT3                 55
#define G_IRQNUM_SPS0_SPSCTRLINT4                 56
#define G_IRQNUM_SPS0_SPSCTRLINT5                 57
#define G_IRQNUM_SPS0_SPSCTRLINT6                 58
#define G_IRQNUM_SPS0_SPSCTRLINT7                 59
#define G_IRQNUM_SPS0_TXFIFO_EMPTY_INTR           60
#define G_IRQNUM_SPS0_TXFIFO_FULL_INTR            61
#define G_IRQNUM_SPS0_TXFIFO_LVL_INTR             62
#define G_IRQNUM_TIMEHS0_TIMINTC                  63
#define G_IRQNUM_TIMEHS0_TIMINT1                  64
#define G_IRQNUM_TIMEHS0_TIMINT2                  65
#define G_IRQNUM_TIMEHS1_TIMINTC                  66
#define G_IRQNUM_TIMEHS1_TIMINT1                  67
#define G_IRQNUM_TIMEHS1_TIMINT2                  68
#define G_IRQNUM_TIMELS0_TIMINT0                  69
#define G_IRQNUM_TIMELS0_TIMINT1                  70
#define G_IRQNUM_UART0_RXBINT                     71
#define G_IRQNUM_UART0_RXFINT                     72
#define G_IRQNUM_UART0_RXINT                      73
#define G_IRQNUM_UART0_RXOVINT                    74
#define G_IRQNUM_UART0_RXTOINT                    75
#define G_IRQNUM_UART0_TXINT                      76
#define G_IRQNUM_UART0_TXOVINT                    77
#define G_IRQNUM_UART1_RXBINT                     78
#define G_IRQNUM_UART1_RXFINT                     79
#define G_IRQNUM_UART1_RXINT                      80
#define G_IRQNUM_UART1_RXOVINT                    81
#define G_IRQNUM_UART1_RXTOINT                    82
#define G_IRQNUM_UART1_TXINT                      83
#define G_IRQNUM_UART1_TXOVINT                    84
#define G_IRQNUM_UART2_RXBINT                     85
#define G_IRQNUM_UART2_RXFINT                     86
#define G_IRQNUM_UART2_RXINT                      87
#define G_IRQNUM_UART2_RXOVINT                    88
#define G_IRQNUM_UART2_RXTOINT                    89
#define G_IRQNUM_UART2_TXINT                      90
#define G_IRQNUM_UART2_TXOVINT                    91
#define G_IRQNUM_WATCHDOG0_WDOGINT                92

#endif /* __CROS_EC_REGISTERS_H */
