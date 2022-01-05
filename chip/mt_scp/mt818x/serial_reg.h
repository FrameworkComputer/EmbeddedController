/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * UART register map
 */

#ifndef __CROS_EC_SERIAL_REG_H
#define __CROS_EC_SERIAL_REG_H

#include "registers.h"

/* Number of hardware ports */
#define HW_UART_PORTS 2

/* DLAB (Divisor Latch Access Bit) == 0 */

/* Data register
 *   (Read) Rcvr buffer register
 *   (Write) Xmit holding register
 */
#define UART_DATA(n)			UART_REG(n, 0)
/* (Write) Interrupt enable register */
#define UART_IER(n)			UART_REG(n, 1)
#define UART_IER_RDI			BIT(0) /* Recv data int */
#define UART_IER_THRI			BIT(1) /* Xmit holding register int */
#define UART_IER_RLSI			BIT(2) /* Rcvr line status int */
#define UART_IER_MSI			BIT(3) /* Modem status int */
/* (Read) Interrupt ID register */
#define UART_IIR(n)			UART_REG(n, 2)
#define UART_IIR_NO_INT			BIT(0) /* No int pending */
#define UART_IIR_ID_MASK		0x0e /* Interrupt ID mask */
#define UART_IIR_MSI			0x00
#define UART_IIR_THRI			0x02
#define UART_IIR_RDI			0x04
#define UART_IIR_RLSI			0x06
#define UART_IIR_BUSY			0x07 /* DW APB busy */
/* (Write) FIFO control register */
#define UART_FCR(n)			UART_REG(n, 2)
#define UART_FCR_ENABLE_FIFO		BIT(0) /* Enable FIFO */
#define UART_FCR_CLEAR_RCVR		BIT(1) /* Clear rcvr FIFO */
#define UART_FCR_CLEAR_XMIT		BIT(2) /* Clear xmit FIFO */
#define UART_FCR_DMA_SELECT		BIT(3)
/* FIFO trigger levels */
#define UART_FCR_T_TRIG_00		0x00
#define UART_FCR_T_TRIG_01		0x10
#define UART_FCR_T_TRIG_10		0x20
#define UART_FCR_T_TRIG_11		0x30
#define UART_FCR_R_TRIG_00		0x00
#define UART_FCR_R_TRIG_01		0x40
#define UART_FCR_R_TRIG_10		0x80
#define UART_FCR_R_TRIG_11		0x80
/* (Write) Line control register */
#define UART_LCR(n)			UART_REG(n, 3)
#define UART_LCR_WLEN5			0 /* Word length 5 bits */
#define UART_LCR_WLEN6			1
#define UART_LCR_WLEN7			2
#define UART_LCR_WLEN8			3
#define UART_LCR_STOP			BIT(2) /* Stop bits: 1bit, 2bits */
#define UART_LCR_PARITY			BIT(3) /* Parity enable */
#define UART_LCR_EPAR			BIT(4) /* Even parity */
#define UART_LCR_SPAR			BIT(5) /* Stick parity */
#define UART_LCR_SBC			BIT(6) /* Set break control */
#define UART_LCR_DLAB			BIT(7) /* Divisor latch access */
/* (Write) Modem control register */
#define UART_MCR(n)			UART_REG(n, 4)
/* (Read) Line status register */
#define UART_LSR(n)			UART_REG(n, 5)
#define UART_LSR_DR			BIT(0) /* Data ready */
#define UART_LSR_OE			BIT(1) /* Overrun error */
#define UART_LSR_PE			BIT(2) /* Parity error */
#define UART_LSR_FE			BIT(3) /* Frame error */
#define UART_LSR_BI			BIT(4) /* Break interrupt */
#define UART_LSR_THRE			BIT(5) /* Xmit-hold-register empty */
#define UART_LSR_TEMT			BIT(6) /* Xmit empty */
#define UART_LSR_FIFOE			BIT(7) /* FIFO error */

/* DLAB == 1 */

/* (Write) Divisor latch */
#define UART_DLL(n)			UART_REG(n, 0) /* Low */
#define UART_DLH(n)			UART_REG(n, 1) /* High */

/* MTK extension */
#define UART_HIGHSPEED(n)		UART_REG(n, 9)
#define UART_SAMPLE_COUNT(n)		UART_REG(n, 10)
#define UART_SAMPLE_POINT(n)		UART_REG(n, 11)
#define UART_RATE_FIX(n)		UART_REG(n, 13)

#endif /* __CROS_EC_SERIAL_REG_H */
