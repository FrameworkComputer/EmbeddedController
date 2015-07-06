/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "pmu.h"
#include "registers.h"
#include "spi.h"
#include "sps.h"
#include "task.h"

/* SPS Control Mode */
enum sps_mode {
	SPS_GENERIC_MODE = 0,
	SPS_SWETLAND_MODE = 1,
	SPS_ROM_MODE = 2,
	SPS_UNDEF_MODE = 3,
};

#define SPS_FIFO_SIZE		(1 << 10)
#define SPS_FIFO_MASK		(SPS_FIFO_SIZE - 1)
/*
 * Hardware pointers use one extra bit, which means that indexing FIFO and
 * values written into the pointers have to have dfferent sizes. Tracked under
 * http://b/20894690
 */
#define SPS_FIFO_PTR_MASK	((SPS_FIFO_MASK << 1) | 1)

#define SPS_TX_FIFO_BASE_ADDR (GBASE(SPS) + 0x1000)
#define SPS_RX_FIFO_BASE_ADDR (SPS_TX_FIFO_BASE_ADDR + SPS_FIFO_SIZE)

void sps_tx_status(uint8_t byte)
{
	GREG32(SPS, DUMMY_WORD) = byte;
}

int sps_transmit(uint8_t *data, size_t data_size)
{
	volatile uint32_t *sps_tx_fifo;
	uint32_t rptr;
	uint32_t wptr;
	uint32_t fifo_room;
	int bytes_sent;

	sps_tx_fifo = (volatile uint32_t *)SPS_TX_FIFO_BASE_ADDR;

	wptr = GREG32(SPS, TXFIFO_WPTR);
	rptr = GREG32(SPS, TXFIFO_RPTR);
	fifo_room = (rptr - wptr - 1) & SPS_FIFO_MASK;

	if (fifo_room < data_size) {
		bytes_sent = fifo_room;
		data_size = fifo_room;
	} else {
		bytes_sent = data_size;
	}

	sps_tx_fifo += (wptr & SPS_FIFO_MASK) / sizeof(*sps_tx_fifo);

	while (data_size) {

		if ((wptr & 3) || (data_size < 4) || ((uintptr_t)data & 3)) {
			/*
			 * Either we have less then 4 bytes to send, or one of
			 * the pointers is not 4 byte aligned. Need to go byte
			 * by byte.
			 */
			uint32_t fifo_contents;
			int bit_shift;

			fifo_contents = *sps_tx_fifo;
			do {
				/*
				 * CR50 SPS controller does not allow byte
				 * accesses for writes into the FIFO, so read
				 * modify/write is requred. Tracked uder
				 * http://b/20894727
				 */
				bit_shift = 8 * (wptr & 3);
				fifo_contents &= ~(0xff << bit_shift);
				fifo_contents |=
					(((uint32_t)(*data++)) << bit_shift);
				data_size--;
				wptr++;

			} while (data_size && (wptr & 3));

			*sps_tx_fifo++ = fifo_contents;
		} else {
			/*
			 * Both fifo wptr and data are aligned and there is
			 * plenty to send.
			 */
			*sps_tx_fifo++ = *((uint32_t *)data);
			data += 4;
			data_size -= 4;
			wptr += 4;
		}
		GREG32(SPS, TXFIFO_WPTR) = wptr & SPS_FIFO_PTR_MASK;

		/* Make sure FIFO pointer wraps along with the index. */
		if (!(wptr & SPS_FIFO_MASK))
			sps_tx_fifo = (volatile uint32_t *)
				SPS_TX_FIFO_BASE_ADDR;
	}

	return bytes_sent;
}

/*
 * Disable interrupts, clear and reset the HW FIFOs.
 */
static void sps_reset(void)
{
	enum sps_mode mode = SPS_GENERIC_MODE;
	enum spi_clock_mode clk_mode = SPI_CLOCK_MODE0;

	/* Disable All Interrupts */
	GREG32(SPS, ICTRL) = 0;

	GWRITE_FIELD(SPS, CTRL, MODE, mode);
	GWRITE_FIELD(SPS, CTRL, IDLE_LVL, 0);
	GWRITE_FIELD(SPS, CTRL, CPHA, clk_mode & 1);
	GWRITE_FIELD(SPS, CTRL, CPOL, (clk_mode >> 1) & 1);
	GWRITE_FIELD(SPS, CTRL, TXBITOR, 1); /* MSB first */
	GWRITE_FIELD(SPS, CTRL, RXBITOR, 1); /* MSB first */

	/* Default dummy word */
	sps_tx_status(0xff);

	/*
	 * Reset both FIFOs
	 *          [5,  4,   3]          [2,  1,   0]
	 * RX{AUTO_DIS, EN, RST} TX{AUTO_DIS, EN, RST}
	 */
	GREG32(SPS, FIFO_CTRL) = 0x9;

	/* wait for reset to self clear. */
	while (GREG32(SPS, FIFO_CTRL) & 9)
		;
}

/*
 * Following a reset, resume listening for and handling incoming bytes.
 */
static void sps_enable(void)
{
	/* Enable the FIFOs */
	GWRITE_FIELD(SPS, FIFO_CTRL, RXFIFO_EN, 1);
	GWRITE_FIELD(SPS, FIFO_CTRL, TXFIFO_EN, 1);

	/*
	 * Wait until we have a few bytes in the FIFO before waking up. Note
	 * that if the master wants to read bytes from us, it may have to clock
	 * in at least RXFIFO_THRESHOLD + 1 bytes before we notice that it's
	 * asking.
	 */
	GREG32(SPS, RXFIFO_THRESHOLD) = 8;
	GWRITE_FIELD(SPS, ICTRL, RXFIFO_LVL, 1);

	/* Also wake up when the master has finished talking to us, so we can
	 * drain any remaining bytes in the RX FIFO. Too late for TX, of
	 * course. */
	GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
	GWRITE_FIELD(SPS, ICTRL, CS_DEASSERT, 1);
}

/*
 * Check how much data is available in the RX FIFO and return a pointer to the
 * available data and its size.
 *
 * @param data   pointer to set to the beginning of data in the fifo
 * @return       number of available bytes
 * zero
 */
static int sps_check_rx(uint8_t **data)
{
	uint32_t write_ptr = GREG32(SPS, RXFIFO_WPTR) & SPS_FIFO_MASK;
	uint32_t read_ptr = GREG32(SPS, RXFIFO_RPTR) & SPS_FIFO_MASK;

	if (read_ptr == write_ptr)
		return 0;

	*data = (uint8_t *)(SPS_RX_FIFO_BASE_ADDR + read_ptr);

	if (read_ptr > write_ptr)
		return SPS_FIFO_SIZE - read_ptr;

	return write_ptr - read_ptr;
}

/* Advance RX FIFO read pointer after data has been read from the FIFO. */
static void sps_advance_rx(int data_size)
{
	uint32_t read_ptr = GREG32(SPS, RXFIFO_RPTR) + data_size;

	GREG32(SPS, RXFIFO_RPTR) = read_ptr & SPS_FIFO_PTR_MASK;
}

/* RX FIFO handler. If NULL, incoming bytes are silently discarded. */
static rx_handler_fn sps_rx_handler;

static void sps_rx_interrupt(int cs_enabled)
{
	size_t data_size;
	do {
		uint8_t *received_data;
		data_size = sps_check_rx(&received_data);
		if (sps_rx_handler)
			sps_rx_handler(received_data, data_size, cs_enabled);
		sps_advance_rx(data_size);
	} while (data_size);
}

void _sps0_interrupt(void)
{
	sps_rx_interrupt(1);
	/* The RXFIFO_LVL interrupt clears itself when the level drops */
}
DECLARE_IRQ(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR, _sps0_interrupt, 1);

void _sps0_cs_deassert_interrupt(void)
{
	/* Make sure the receive FIFO is drained. */
	sps_rx_interrupt(0);
	/* Clear the interrupt bit */
	GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
}
DECLARE_IRQ(GC_IRQNUM_SPS0_CS_DEASSERT_INTR, _sps0_cs_deassert_interrupt, 1);

void sps_unregister_rx_handler(void)
{
	task_disable_irq(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR);
	task_disable_irq(GC_IRQNUM_SPS0_CS_DEASSERT_INTR);
	sps_reset();
	sps_rx_handler = NULL;
}

void sps_register_rx_handler(rx_handler_fn func)
{
	sps_unregister_rx_handler();
	sps_rx_handler = func;
	sps_enable();
	task_enable_irq(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR);
	task_enable_irq(GC_IRQNUM_SPS0_CS_DEASSERT_INTR);
}

/* At reset the SPS module is initialized, but not enabled */
static void sps_init(void)
{
	pmu_clock_en(PERIPH_SPS);
	sps_unregister_rx_handler();
}
DECLARE_HOOK(HOOK_INIT, sps_init, HOOK_PRIO_INIT_CHIPSET);
