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

/*
 * Hardware pointers use one extra bit to indicate wrap around. This means we
 * can fill the FIFO completely, but it also means that the FIFO index and the
 * values written into the read/write pointer registers have different sizes.
 */
#define SPS_FIFO_SIZE		1024
#define SPS_FIFO_MASK		(SPS_FIFO_SIZE - 1)
#define SPS_FIFO_PTR_MASK	((SPS_FIFO_MASK << 1) | 1)

/* Just the FIFO-sized part */
#define low(V) ((V) & SPS_FIFO_MASK)

/* Return the number of bytes in the FIFO (0 to SPS_FIFO_SIZE) */
static uint32_t fifo_count(uint32_t readptr, uint32_t writeptr)
{
	uint32_t tmp = readptr ^ writeptr;

	if (!tmp)				/* completely equal == empty */
		return 0;

	if (!low(tmp))				/* only high bit diff == full */
		return SPS_FIFO_SIZE;

	return low(writeptr - readptr);		/* else just |diff| */
}

/* HW FIFO buffer addresses */
#define SPS_TX_FIFO_BASE_ADDR (GBASE(SPS) + 0x1000)
#define SPS_RX_FIFO_BASE_ADDR (SPS_TX_FIFO_BASE_ADDR + SPS_FIFO_SIZE)

#ifdef CONFIG_SPS_TEST
/* Statistics counters. Not always present, to save space & time. */
uint32_t sps_tx_count, sps_rx_count, sps_tx_empty_count, sps_max_rx_batch;
#endif

void sps_tx_status(uint8_t byte)
{
	GREG32(SPS, DUMMY_WORD) = byte;
}

int sps_transmit(uint8_t *data, size_t data_size)
{
	volatile uint32_t *sps_tx_fifo32;
	uint32_t rptr;
	uint32_t wptr;
	uint32_t fifo_room;
	int bytes_sent;

#ifdef CONFIG_SPS_TEST
	if (GREAD_FIELD(SPS, ISTATE, TXFIFO_EMPTY))
		sps_tx_empty_count++;
#endif

	wptr = GREG32(SPS, TXFIFO_WPTR);
	rptr = GREG32(SPS, TXFIFO_RPTR);
	fifo_room = SPS_FIFO_SIZE - fifo_count(rptr, wptr);

	if (fifo_room < data_size)
		data_size = fifo_room;
	bytes_sent = data_size;

	/* Need 32-bit pointers for issue b/20894727 */
	sps_tx_fifo32 = (volatile uint32_t *)SPS_TX_FIFO_BASE_ADDR;
	sps_tx_fifo32 += (wptr & SPS_FIFO_MASK) / sizeof(*sps_tx_fifo32);

	while (data_size) {

		if ((wptr & 3) || (data_size < 4) || ((uintptr_t)data & 3)) {
			/*
			 * Either we have less then 4 bytes to send, or one of
			 * the pointers is not 4 byte aligned. Need to go byte
			 * by byte.
			 */
			uint32_t fifo_contents;
			int bit_shift;

			fifo_contents = *sps_tx_fifo32;
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

			*sps_tx_fifo32++ = fifo_contents;
		} else {
			/*
			 * Both fifo wptr and data are aligned and there is
			 * plenty to send.
			 */
			*sps_tx_fifo32++ = *((uint32_t *)data);
			data += 4;
			data_size -= 4;
			wptr += 4;
		}
		GREG32(SPS, TXFIFO_WPTR) = wptr & SPS_FIFO_PTR_MASK;

		/* Make sure FIFO pointer wraps along with the index. */
		if (!low(wptr))
			sps_tx_fifo32 = (volatile uint32_t *)
				SPS_TX_FIFO_BASE_ADDR;
	}

#ifdef CONFIG_SPS_TEST
	sps_tx_count += bytes_sent;
#endif
	return bytes_sent;
}

/*
 * Disable interrupts, clear and reset the HW FIFOs.
 */
static void sps_reset(enum spi_clock_mode m_spi, enum sps_mode m_sps)

{
	/* Disable All Interrupts */
	GREG32(SPS, ICTRL) = 0;

	GWRITE_FIELD(SPS, CTRL, MODE, m_sps);
	GWRITE_FIELD(SPS, CTRL, IDLE_LVL, 0);
	GWRITE_FIELD(SPS, CTRL, CPHA, m_spi & 1);
	GWRITE_FIELD(SPS, CTRL, CPOL, (m_spi >> 1) & 1);
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
	 * Wait until we have a few bytes in the FIFO before waking up. There's
	 * a tradeoff here: If the master wants to talk to us it will have to
	 * clock in at least RXFIFO_THRESHOLD + 1 bytes before we notice. On
	 * the other hand, if we set this too low we waste a lot of time
	 * handling interrupts before we have enough bytes to know what the
	 * master is saying.
	 */
	GREG32(SPS, RXFIFO_THRESHOLD) = 7;
	GWRITE_FIELD(SPS, ICTRL, RXFIFO_LVL, 1);

	/* Also wake up when the master has finished talking to us, so we can
	 * drain any remaining bytes in the RX FIFO. */
	GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
	GWRITE_FIELD(SPS, ICTRL, CS_DEASSERT, 1);
}

/*
 * Check how much LINEAR data is available in the RX FIFO and return a pointer
 * to the data and its size. If the FIFO data wraps around the end of the
 * physical address space, this only returns the amount up to the the end of
 * the buffer.
 *
 * @param data   pointer to set to the beginning of data in the fifo
 * @return       number of available bytes
 * zero
 */
static int sps_check_rx(uint8_t **data)
{
	uint32_t wptr = GREG32(SPS, RXFIFO_WPTR);
	uint32_t rptr = GREG32(SPS, RXFIFO_RPTR);
	uint32_t count = fifo_count(rptr, wptr);

	if (!count)
		return 0;

	wptr = low(wptr);
	rptr = low(rptr);

	if (rptr >= wptr)
		count = SPS_FIFO_SIZE - rptr;
	else
		count = wptr - rptr;

	*data = (uint8_t *)(SPS_RX_FIFO_BASE_ADDR + rptr);

	return count;
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
#ifdef CONFIG_SPS_TEST
		sps_rx_count += data_size;
		if (data_size > sps_max_rx_batch)
			sps_max_rx_batch = data_size;
#endif
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
	/* Notify the registered handler (and drain the RX FIFO) */
	sps_rx_interrupt(0);
	/* Clear the TX FIFO manually, so the next transaction doesn't
	 * start by clocking out any bytes left over from this one. */
	GREG32(SPS, TXFIFO_WPTR) = GREG32(SPS, TXFIFO_RPTR);
	/* Clear the interrupt bit */
	GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
}
DECLARE_IRQ(GC_IRQNUM_SPS0_CS_DEASSERT_INTR, _sps0_cs_deassert_interrupt, 1);

void sps_unregister_rx_handler(void)
{
	task_disable_irq(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR);
	task_disable_irq(GC_IRQNUM_SPS0_CS_DEASSERT_INTR);
	sps_rx_handler = NULL;

	/* The modes don't really matter since we're disabling interrupts.
	 * Mostly we just want to reset the FIFOs. */
	sps_reset(SPI_CLOCK_MODE0, SPS_GENERIC_MODE);
}

void sps_register_rx_handler(enum spi_clock_mode m_spi,
			     enum sps_mode m_sps,
			     rx_handler_fn func)
{
	task_disable_irq(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR);
	task_disable_irq(GC_IRQNUM_SPS0_CS_DEASSERT_INTR);
	sps_reset(m_spi, m_sps);
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
