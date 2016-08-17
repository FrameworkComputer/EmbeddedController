/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for Rotor MCU
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"

/* Master clocks and resets. */
#define ROTOR_MCU_CLKRSTGEN_BASE	0xEF000800
#define ROTOR_MCU_RESETAP		REG32(ROTOR_MCU_CLKRSTGEN_BASE + 0x000)
#define ROTOR_MCU_AP_NRESET		(1 << 0)
#define ROTOR_MCU_M4_BIST_CLKCFG	REG32(ROTOR_MCU_CLKRSTGEN_BASE + 0x140)
#define ROTOR_MCU_M4_BIST_CLKEN		(1 << 1)

/* GPIO */
#define DUMMY_GPIO_BANK 0

#define GPIO_A 0xEF022000
#define GPIO_B 0xEF022100
#define GPIO_C 0xEF022200
#define GPIO_D 0xEF022300
#define GPIO_E 0xEF022400

#define ROTOR_MCU_GPIO_PLR(b)		REG32((b) + 0x0)
#define ROTOR_MCU_GPIO_PDR(b)		REG32((b) + 0x4)
#define ROTOR_MCU_GPIO_PSR(b)		REG32((b) + 0xC)
#define ROTOR_MCU_GPIO_HRIPR(b)		REG32((b) + 0x10)
#define ROTOR_MCU_GPIO_LFIPR(b)		REG32((b) + 0x14)
#define ROTOR_MCU_GPIO_ISR(b)		REG32((b) + 0x18)
#define ROTOR_MCU_GPIO_SDR(b)		REG32((b) + 0x1C)
#define ROTOR_MCU_GPIO_CDR(b)		REG32((b) + 0x20)
#define ROTOR_MCU_GPIO_SHRIPR(b)	REG32((b) + 0x24)
#define ROTOR_MCU_GPIO_CHRIPR(b)	REG32((b) + 0x28)
#define ROTOR_MCU_GPIO_SLFIPR(b)	REG32((b) + 0x2C)
#define ROTOR_MCU_GPIO_CLFIPR(b)	REG32((b) + 0x30)
#define ROTOR_MCU_GPIO_OLR(b)		REG32((b) + 0x34)
#define ROTOR_MCU_GPIO_DWER(b)		REG32((b) + 0x38)
#define ROTOR_MCU_GPIO_IMR(b)		REG32((b) + 0x3C)
#define ROTOR_MCU_GPIO_SIMR(b)		REG32((b) + 0x48)
#define ROTOR_MCU_GPIO_CIMR(b)		REG32((b) + 0x4C)
/* Interrupt Target Enable regs for MCU are instances 24-31. */
#define ROTOR_MCU_GPIO_ITER(b)		REG32((b) + 0xB0 + \
					      4*(((b) - GPIO_A) >> 8))


/* MCU Pad Wrap */
#define ROTOR_MCU_PAD_WRAP_BASE	0xEF020000
#define ROTOR_MCU_IO_PAD_CFG(n)	REG32(ROTOR_MCU_PAD_WRAP_BASE + 0x8 + \
				      ((n) * 0x4))

#define GPIO_PAD_CFG_IDX(port, pin)	(((((port) % 0x2000) / 0x100) * 32) + \
					 (pin))
#define GPIO_PAD_CFG_ADDR(port, pin)	((GPIO_PAD_CFG_IDX(port, pin) * 4) + \
					 ROTOR_MCU_PAD_WRAP_BASE + 8)
#define ROTOR_MCU_GPIO_PCFG(port, pin)	REG32(GPIO_PAD_CFG_ADDR(port, pin))

/* I2C */
#define ROTOR_MCU_I2C_BASE		0xED080000
#define ROTOR_MCU_I2C_CFG_BASE(n)	(ROTOR_MCU_I2C_BASE + (n)*0x1000)
#define ROTOR_MCU_I2C_CON(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x00)
#define ROTOR_MCU_I2C_TAR(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x04)
#define ROTOR_MCU_I2C_SAR(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x08)
#define ROTOR_MCU_I2C_HS_MADDR(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x0C)
#define ROTOR_MCU_I2C_DATA_CMD(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x10)
#define ROTOR_MCU_I2C_SS_SCL_HCNT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x14)
#define ROTOR_MCU_I2C_SS_SCL_LCNT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x18)
#define ROTOR_MCU_I2C_FS_SCL_HCNT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x1C)
#define ROTOR_MCU_I2C_FS_SCL_LCNT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x20)
#define ROTOR_MCU_I2C_HS_SCL_HCNT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x24)
#define ROTOR_MCU_I2C_HS_SCL_LCNT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x28)
#define ROTOR_MCU_I2C_INTR_STAT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x2C)
#define ROTOR_MCU_I2C_INTR_MASK(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x30)
#define ROTOR_MCU_I2C_RAW_INTR_STAT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x34)
#define ROTOR_MCU_I2C_RX_TL(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x38)
#define ROTOR_MCU_I2C_TX_TL(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x3C)
#define ROTOR_MCU_I2C_CLR_INTR(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x40)
#define ROTOR_MCU_I2C_CLR_RX_UNDER(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x44)
#define ROTOR_MCU_I2C_CLR_RX_OVER(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x48)
#define ROTOR_MCU_I2C_CLR_TX_OVER(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x4C)
#define ROTOR_MCU_I2C_CLR_RD_REQ(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x50)
#define ROTOR_MCU_I2C_CLR_TX_ABRT(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x54)
#define ROTOR_MCU_I2C_CLR_RX_DONE(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x58)
#define ROTOR_MCU_I2C_CLR_ACTIVITY(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x5C)
#define ROTOR_MCU_I2C_CLR_STOP_DET(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x60)
#define ROTOR_MCU_I2C_CLR_START_DET(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x64)
#define ROTOR_MCU_I2C_CLR_GEN_CALL(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x68)
#define ROTOR_MCU_I2C_ENABLE(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x6C)
#define ROTOR_MCU_I2C_STATUS(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x70)
#define ROTOR_MCU_I2C_TXFLR(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x74)
#define ROTOR_MCU_I2C_RXFLR(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x78)
#define ROTOR_MCU_I2C_SDA_HOLD(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x7C)
#define ROTOR_MCU_I2C_TX_ABRT_SRC(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x80)
#define ROTOR_MCU_I2C_DMA_CR(n)		REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x88)
#define ROTOR_MCU_I2C_DMA_TDLR(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x8C)
#define ROTOR_MCU_I2C_DMA_RDLR(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x90)
#define ROTOR_MCU_I2C_SDA_SETUP(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x94)
#define ROTOR_MCU_I2C_ACK_GEN_CALL(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x98)
#define ROTOR_MCU_I2C_ENABLE_STATUS(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0x9C)
#define ROTOR_MCU_I2C_FS_SPKLEN(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0xA0)
#define ROTOR_MCU_I2C_HS_SPKLEN(n)	REG32(ROTOR_MCU_I2C_CFG_BASE(n) + 0xA4)
#define ROTOR_MCU_I2C_REFCLKGEN(n)	REG32((ROTOR_MCU_CLKRSTGEN_BASE + \
					       + 0x3D0 + (0x10 * (n))))
/* bit definitions */
#define ROTOR_MCU_I2C_M_TX_ABRT		(1 << 6)
#define ROTOR_MCU_I2C_M_TX_EMPTY	(1 << 4)
#define ROTOR_MCU_I2C_M_RX_FULL		(1 << 2)
#define ROTOR_MCU_I2C_ABORT		(1 << 1)
#define ROTOR_MCU_I2C_EN		(1 << 0)
#define ROTOR_MCU_I2C_IC_EN		(1 << 0)
#define ROTOR_MCU_I2C_STOP		(1 << 9)
#define ROTOR_MCU_I2C_RD_CMD		(1 << 8)
#define ROTOR_MCU_I2C_RESTART		(1 << 10)
#define ROTOR_MCU_I2C_SPEED_STD_MODE	(1 << 1)
#define ROTOR_MCU_I2C_SPEED_FAST_MODE	(2 << 1)
#define ROTOR_MCU_I2C_SPEED_HISPD_MODE	(3 << 1)
#define ROTOR_MCU_I2C_IC_SLAVE_DISABLE	(1 << 6)
#define ROTOR_MCU_I2C_IC_RESTART_EN	(1 << 5)
#define ROTOR_MCU_I2C_MASTER_MODE	(1 << 0)


/* UART */
#define ROTOR_MCU_UART0_CLKGEN		REG32(ROTOR_MCU_CLKRSTGEN_BASE + 0x240)
#define ROTOR_MCU_UART0_REFCLKGEN	REG32(ROTOR_MCU_CLKRSTGEN_BASE + 0x3B0)
#define ROTOR_MCU_UART_CFG_BASE(n)	(0xED060000 + (n)*0x1000)
/* DLAB = 0 */
#define ROTOR_MCU_UART_RBR(n) /* R */	REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x0)
#define ROTOR_MCU_UART_THR(n) /* W */	REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x0)
#define ROTOR_MCU_UART_IER(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x4)
/* DLAB = 1 */
#define ROTOR_MCU_UART_DLL(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x0)
#define ROTOR_MCU_UART_DLH(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x4)

#define ROTOR_MCU_UART_IIR(n) /* R */	REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x8)
#define ROTOR_MCU_UART_FCR(n) /* W */	REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x8)
#define ROTOR_MCU_UART_LCR(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0xC)
#define ROTOR_MCU_UART_MCR(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x10)
#define ROTOR_MCU_UART_LSR(n) /* R */	REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x14)
#define ROTOR_MCU_UART_MSR(n) /* R */	REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x18)
#define ROTOR_MCU_UART_SCR(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x1C)
#define ROTOR_MCU_UART_USR(n)		REG32(ROTOR_MCU_UART_CFG_BASE(n) + 0x7C)

/* Timers */
#define ROTOR_MCU_TMR_CFG_BASE(n)	(0xED020000 + (n)*0x1000)
#define ROTOR_MCU_TMR_TNLC(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0x0)
#define ROTOR_MCU_TMR_TNCV(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0x4)
#define ROTOR_MCU_TMR_TNCR(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0x8)
#define ROTOR_MCU_TMR_TNEOI(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0xC)
#define ROTOR_MCU_TMR_TNIS(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0x10)
#define ROTOR_MCU_TMR_TIS(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0xA0)
#define ROTOR_MCU_TMR_TEOI(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0xA4)
#define ROTOR_MCU_TMR_TRIS(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0xA8)
#define ROTOR_MCU_TMR_TNLC2(n)		REG32(ROTOR_MCU_TMR_CFG_BASE(n) + 0xB0)

/* Watchdog */
#define ROTOR_MCU_WDT_BASE		0xED010000
#define ROTOR_MCU_WDT_CR		REG32(ROTOR_MCU_WDT_BASE + 0x00)
#define ROTOR_MCU_WDT_TORR		REG32(ROTOR_MCU_WDT_BASE + 0x04)
#define ROTOR_MCU_WDT_CCVR		REG32(ROTOR_MCU_WDT_BASE + 0x08)
#define ROTOR_MCU_WDT_CRR		REG32(ROTOR_MCU_WDT_BASE + 0x0C)
#define ROTOR_MCU_WDT_STAT		REG32(ROTOR_MCU_WDT_BASE + 0x10)
#define ROTOR_MCU_WDT_EOI		REG32(ROTOR_MCU_WDT_BASE + 0x14)
/* To prevent accidental restarts, this magic value must be written to CRR. */
#define ROTOR_MCU_WDT_KICK		0x76

/* SSI */
#define ROTOR_MCU_SSI_BASE(port)	(0xED070000 + ((port) * 0x1000))
#define ROTOR_MCU_SSI_CTRLR0(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x00)
#define ROTOR_MCU_SSI_CTRLR1(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x04)
#define ROTOR_MCU_SSI_SSIENR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x08)
#define ROTOR_MCU_SSI_BAUDR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x14)
#define ROTOR_MCU_SSI_TXFTLR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x18)
#define ROTOR_MCU_SSI_RXFTLR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x1C)
#define ROTOR_MCU_SSI_TXFLR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x20)
#define ROTOR_MCU_SSI_RXFLR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x24)
#define ROTOR_MCU_SSI_SR(port)		REG32(ROTOR_MCU_SSI_BASE(port) + 0x28)
#define ROTOR_MCU_SSI_IMR(port)		REG32(ROTOR_MCU_SSI_BASE(port) + 0x2C)
#define ROTOR_MCU_SSI_ISR(port)		REG32(ROTOR_MCU_SSI_BASE(port) + 0x30)
#define ROTOR_MCU_SSI_RISR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x34)
#define ROTOR_MCU_SSI_TXOICR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x38)
#define ROTOR_MCU_SSI_RXOICR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x3C)
#define ROTOR_MCU_SSI_RXUICR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x40)
#define ROTOR_MCU_SSI_ICR(port)		REG32(ROTOR_MCU_SSI_BASE(port) + 0x48)
#define ROTOR_MCU_SSI_DMACR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x4C)
#define ROTOR_MCU_SSI_DMATDLR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x50)
#define ROTOR_MCU_SSI_DMARDLR(port)	REG32(ROTOR_MCU_SSI_BASE(port) + 0x54)
#define ROTOR_MCU_SSI_IDR(port)		REG32(ROTOR_MCU_SSI_BASE(port) + 0x58)
#define ROTOR_MCU_SSI_DR(port, idx)	REG32(ROTOR_MCU_SSI_BASE(port) + \
					      (0x60 + ((idx) * 0x04)))
#define ROTOR_MCU_MAX_SSI_PORTS 2

/* DMA */
#define ROTOR_MCU_DMA_BASE 0xED200000

enum dma_channel {
	/* Channel numbers */
	ROTOR_MCU_DMAC_SPI0_TX =	0,
	ROTOR_MCU_DMAC_SPI0_RX =	1,
	ROTOR_MCU_DMAC_SPI1_TX =	2,
	ROTOR_MCU_DMAC_SPI1_RX =	3,

	/* Channel count */
	ROTOR_MCU_DMAC_COUNT =		8,
};

/* Registers for a single channel of the DMA controller. */
struct rotor_mcu_dma_chan {
	uint32_t cfg;			/* Config */
	uint32_t ctrl;			/* Control */
	uint32_t status;		/* Status */
	uint32_t pad0;
	uint32_t cpr;			/* Parameter */
	uint32_t cdr;			/* Descriptor */
	uint32_t cndar;			/* Next descriptor address */
	uint32_t fill_value;		/* Fill value */
	uint32_t int_en;		/* Interrupt enable */
	uint32_t int_pend;		/* Interrupt pending */
	uint32_t int_ack;		/* Interrupt acknowledge */
	uint32_t int_force;		/* Interrupt force */
	uint32_t tmr_ctrl;		/* Timer control */
	uint32_t timeout_cnt_stat;	/* Timeout Count Status */
	uint32_t crbar;			/* Read burst address */
	uint32_t crblr;			/* Read burst length */
	uint32_t cwbar;			/* Write burst address */
	uint32_t cwblr;			/* Write burst length */
	uint32_t cwbrr;			/* Write burst remain */
	uint32_t csrr;			/* Save/restore control */
	uint32_t csrli;			/* Save/restore lower DMA ID */
	uint32_t csrui;			/* Save/restore upper DMA ID */
	uint32_t crsl;			/* Lower request status */
	uint32_t crsu;			/* Upper request status */
	uint32_t cafr;			/* ACK force */
	uint32_t pad1[0x27];		/* pad to offset 0x100 */
};

/* Always use rotor_mcu_dma_chan_t so volatile keyword is included! */
typedef volatile struct rotor_mcu_dma_chan rotor_mcu_dma_chan_t;

/* Common code and header file must use this */
typedef rotor_mcu_dma_chan_t dma_chan_t;

struct rotor_mcu_dma_regs {
	uint32_t top_int_status;
	uint32_t top_soft_reset;
	uint32_t params;
	uint32_t pad[0x3D];	/* Pad to offset 0x100 */
	rotor_mcu_dma_chan_t chan[ROTOR_MCU_DMAC_COUNT];
};

/* Always use rotor_mcu_dma_regs_t so volatile keyword is included! */
typedef volatile struct rotor_mcu_dma_regs rotor_mcu_dma_regs_t;

#define ROTOR_MCU_DMA_REGS ((rotor_mcu_dma_regs_t *)ROTOR_MCU_DMA_BASE)

/* IRQ Numbers */
#define ROTOR_MCU_IRQ_TIMER_0		6
#define ROTOR_MCU_IRQ_TIMER_1		7
#define ROTOR_MCU_IRQ_WDT		14
#define ROTOR_MCU_IRQ_UART_0		16
#define ROTOR_MCU_IRQ_SPI_0		18
#define ROTOR_MCU_IRQ_SPI_1		19
#define ROTOR_MCU_IRQ_I2C_0		20
#define ROTOR_MCU_IRQ_I2C_1		21
#define ROTOR_MCU_IRQ_I2C_2		22
#define ROTOR_MCU_IRQ_I2C_3		23
#define ROTOR_MCU_IRQ_I2C_4		24
#define ROTOR_MCU_IRQ_I2C_5		25
#define ROTOR_MCU_IRQ_DMAC_0		44
#define ROTOR_MCU_IRQ_DMAC_1		45
#define ROTOR_MCU_IRQ_DMAC_2		46
#define ROTOR_MCU_IRQ_DMAC_3		47
#define ROTOR_MCU_IRQ_DMAC_4		48
#define ROTOR_MCU_IRQ_DMAC_5		49
#define ROTOR_MCU_IRQ_DMAC_6		50
#define ROTOR_MCU_IRQ_DMAC_7		51
#define ROTOR_MCU_IRQ_DMATOP		52
#define ROTOR_MCU_IRQ_GPIO_0		79
#define ROTOR_MCU_IRQ_GPIO_1		80
#define ROTOR_MCU_IRQ_GPIO_2		81
#define ROTOR_MCU_IRQ_GPIO_3		82
#define ROTOR_MCU_IRQ_GPIO_4		83
#define ROTOR_MCU_IRQ_GPIO_5		84
#define ROTOR_MCU_IRQ_GPIO_6		85
#define ROTOR_MCU_IRQ_GPIO_7		86

#endif /* __CROS_EC_REGISTERS_H */
