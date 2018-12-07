/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for ISH */

#ifndef __CROS_EC_UART_DEFS_H_
#define __CROS_EC_UART_DEFS_H_

#include <stdint.h>
#include <stddef.h>

#define	UART_ERROR		-1
#define UART_BUSY		-2
#define HSU_BASE		ISH_UART_BASE
#define UART0_OFFS              (0x80)
#define UART0_BASE              (ISH_UART_BASE + UART0_OFFS)
#define UART0_SIZE              (0x80)

#define UART1_OFFS              (0x100)
#define UART1_BASE              (ISH_UART_BASE + UART1_OFFS)
#define UART1_SIZE              (0x80)

#define UART2_OFFS              (0x180)
#define UART2_BASE              (ISH_UART_BASE + UART2_OFFS)
#define UART2_SIZE              (0x80)

/* Register accesses */
#define LSR(n)  (uart_ctx[n].base + UART_REG_LSR * uart_ctx[n].addr_interval)
#define THR(n)  (uart_ctx[n].base + UART_REG_THR * uart_ctx[n].addr_interval)
#define FOR(n)  (uart_ctx[n].base + UART_REG_FOR * uart_ctx[n].addr_interval)
#define RBR(n)  (uart_ctx[n].base + UART_REG_RBR * uart_ctx[n].addr_interval)
#define DLL(n)  (uart_ctx[n].base + UART_REG_DLL * uart_ctx[n].addr_interval)
#define DLH(n)  (uart_ctx[n].base + UART_REG_DLH * uart_ctx[n].addr_interval)
#define DLD(n)  (uart_ctx[n].base + UART_REG_DLD * uart_ctx[n].addr_interval)
#define IER(n)  (uart_ctx[n].base + UART_REG_IER * uart_ctx[n].addr_interval)
#define IIR(n)  (uart_ctx[n].base + UART_REG_IIR * uart_ctx[n].addr_interval)
#define FCR(n)  (uart_ctx[n].base + UART_REG_FCR * uart_ctx[n].addr_interval)
#define LCR(n)  (uart_ctx[n].base + UART_REG_LCR * uart_ctx[n].addr_interval)
#define MCR(n)  (uart_ctx[n].base + UART_REG_MCR * uart_ctx[n].addr_interval)
#define MSR(n)  (uart_ctx[n].base + UART_REG_MSR * uart_ctx[n].addr_interval)
#define FCTR(n)  (uart_ctx[n].base + UART_REG_FCTR * uart_ctx[n].addr_interval)
#define EFR(n)  (uart_ctx[n].base + UART_REG_EFR * uart_ctx[n].addr_interval)
#define RXTRG(n) \
		(uart_ctx[n].base + UART_REG_RXTRG * uart_ctx[n].addr_interval)
#define ABR(n)  (uart_ctx[n].base + UART_REG_ABR * uart_ctx[n].addr_interval)
#define PS(n)  (uart_ctx[n].base + UART_REG_PS * uart_ctx[n].addr_interval)
#define MUL(n)  (uart_ctx[n].base + UART_REG_MUL * uart_ctx[n].addr_interval)
#define DIV(n)  (uart_ctx[n].base + UART_REG_DIV * uart_ctx[n].addr_interval)

/* RBR: Receive Buffer register     (BLAB bit = 0)  */
#define UART_REG_RBR		(0)
/* THR: Transmit Holding register   (BLAB bit = 0)  */
#define UART_REG_THR		(0)
/* IER: Interrupt Enable register   (BLAB bit = 0)  */
#define UART_REG_IER		(1)

#define FCR_FIFO_SIZE_16	(0x00)
#define FCR_FIFO_SIZE_64	(0x20)
#define FCR_ITL_FIFO_64_BYTES_1	(0x00)

/* FCR: FIFO Control register */
#define UART_REG_FCR		(2)
#define FCR_FIFO_ENABLE		(0x01)
#define FCR_RESET_RX		(0x02)
#define FCR_RESET_TX		(0x04)

/* LCR: Line Control register */
#define UART_REG_LCR		(3)
#define LCR_DLAB                (0x80)
#define LCR_5BIT_CHR            (0x00)
#define LCR_6BIT_CHR            (0x01)
#define LCR_7BIT_CHR            (0x02)
#define LCR_8BIT_CHR            (0x03)
#define LCR_BIT_CHR_MASK        (0x03)
#define LCR_SB                  (0x40)	/*Set Break */

/* MCR: Modem Control register */
#define UART_REG_MCR		(4)
#define MCR_DTR			(0x1)
#define MCR_RTS			(0x2)
#define MCR_LOO			(0x10)
#define MCR_INTR_ENABLE		(0x08)
#define MCR_AUTO_FLOW_EN	(0x20)

/* LSR: Line Status register */
#define UART_REG_LSR		(5)
#define LSR_DR			(0x01)	/* Data Ready */
#define LSR_OE			(0x02)	/* Overrun error */
#define LSR_PE			(0x04)	/* Parity error */
#define LSR_FE			(0x08)	/* Framing error */
#define LSR_BI			(0x10)	/* Breaking interrupt */
#define LSR_THR_EMPTY		(0x20)	/* Non FIFO mode: Transmit holding
					 * register empty
					 */
#define LSR_TDRQ		(0x20)	/* FIFO mode: Transmit Data request */
#define LSR_TEMT		(0x40)	/* Transmitter empty */

#define FCR_ITL_FIFO_64_BYTES_56    (0xc0)

#define IER_RECV		(0x01)
#define IER_TDRQ		(0x02)
#define IER_LINE_STAT		(0x04)

#define UART_REG_IIR		(2)
/* MSR: Modem Status register */
#define UART_REG_MSR		(6)

/* DLL: Divisor Latch Reg. low byte  (BLAB bit = 1) */
#define UART_REG_DLL		(0)

/* DLH: Divisor Latch Reg. high byte (BLAB bit = 1) */
#define UART_REG_DLH		(1)

/* DLH: Divisor Latch Fractional. (BLAB bit = 1) */
#define UART_REG_DLD		(2)

/* FOR: Fifo O Register (ISH only) */
#define UART_REG_FOR		(0x20)
#define FOR_OCCUPANCY_OFFS      0
#define FOR_OCCUPANCY_MASK      0x7F

/* ABR: Auto-Baud Control Register (ISH only) */
#define UART_REG_ABR		(0x24)
#define ABR_UUE			(0x10)

/* Pre-Scalar Register (ISH only) */
#define UART_REG_PS		(0x30)

/* DDS registers (ISH only) */
#define UART_REG_MUL		(0x34)
#define UART_REG_DIV		(0x38)

/* G_IEN: Global Interrupt Enable  (ISH only) */
#define HSU_REG_GIEN		(0)
#define HSU_REG_GIST		(4)

#define GIEN_PWR_MGMT           (0x01000000)
#define GIEN_DMA_EN             (0x00000020)
#define GIEN_UART2_EN           (0x00000004)
#define GIEN_UART1_EN           (0x00000002)
#define GIEN_UART0_EN           (0x00000001)
#define GIST_DMA_EN             (0x00000020)
#define GIST_UART2_EN           (0x00000004)
#define GIST_UART1_EN           (0x00000002)
#define GIST_UART0_EN           (0x00000001)
#define GIST_UARTx_EN           (GIST_UART0_EN|GIST_UART1_EN|GIST_UART2_EN)

/* UART config flag, send to sc_io_control if the current UART line has HW
 * flow control lines connected.
 */
#define UART_CONFIG_HW_FLOW_CONTROL       (1<<0)

 /* UART config flag for sc_io_control.  If defined a sc_io_event_rx_msg is
  * raised only when the rx buffer is completely full. Otherwise, the event
  * is raised after a timeout is received on the UART line,
  * and all data received until now is provided.
  */
#define UART_CONFIG_DELIVER_FULL_RX_BUF   (1<<1)

/* UART config flag for sc_io_control.  If defined a sc_io_event_rx_buf_depleted
 * is raised when all rx buffers that were added are full. Otherwise, no
 * event is raised.
 */
#define UART_CONFIG_ANNOUNCE_DEPLETED_BUF (1<<2)

#define UART_INT_DEVICES		2
#define UART_EXT_DEVICES		8
#define UART_DEVICES			UART_INT_DEVICES
#define UART_ISH_ADDR_INTERVAL		1

#define B9600				0x0000d
#define B57600				0x00000018
#define B115200				0x00000011
#define B921600				0x00000012
#define B2000000			0x00000013
#define B3000000			0x00000014
#define B3250000			0x00000015
#define B3500000			0x00000016
#define B4000000			0x00000017
#define B19200				0x0000e
#define B38400				0x0000f

/* KHZ, MHZ */
#define KHZ(x)				((x) * 1000)
#define MHZ(x)				(KHZ(x) * 1000)
#if defined(CHIP_FAMILY_ISH3) || defined(CHIP_FAMILY_ISH5)
#define UART_ISH_INPUT_FREQ		MHZ(120)
#elif defined(CHIP_FAMILY_ISH4)
#define UART_ISH_INPUT_FREQ		MHZ(100)
#endif
#define UART_DEFAULT_BAUD_RATE		115200
#define UART_STATE_CG			(1 << UART_OP_CG)

enum UART_PORT {
	UART_PORT_0,
	UART_PORT_1,
	UART_PORT_MAX
};

enum UART_OP {
	UART_OP_READ,
	UART_OP_WRITE,
	UART_OP_CG,
	UART_OP_MAX
};

enum {
	BAUD_IDX,
	BAUD_SPEED,
	BAUD_TABLE_MAX
};

struct uart_ctx {
	uint32_t id;
	uint32_t base;
	uint32_t addr_interval;
	uint32_t uart_state;
	uint32_t is_open;
	uint32_t baud_rate;
	uint32_t input_freq;
	uint32_t client_flags;
};

#endif	/* _CROS_EC_UART_DEFS_H_ */
