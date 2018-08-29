/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "pmu.h"
#include "registers.h"
#include "sps.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

/*
 * This file is a driver for the CR50 SPS (SPI slave) controller. The
 * controller deploys a 2KB buffer split evenly between receive and transmit
 * directions.
 *
 * Each one kilobyte of memory is organized into a FIFO with read
 * and write pointers. RX FIFO write and TX FIFO read pointers are managed by
 * hardware. RX FIFO read and TX FIFO write pointers are managed by
 * software.
 *
 * As of time of writing, TX fifo allows only 32 bit wide write accesses,
 * which makes the function feeding the FIFO unnecessarily complicated.
 *
 * Even though both FIFOs are 1KByte in size, the hardware pointers
 * controlling access to the FIFOs are 11 bits in size, this is another issue
 * requiring special software handling.
 *
 * The driver API includes three functions:
 *
 * - transmit a packet of a certain size, runs on the task context and can
 *   exit before the entire packet is transmitted.,
 *
 * - register a receive callback. The callback is running in interrupt
 *   context. Registering the callback (re)initializes the interface.
 *
 * - unregister receive callback.
 */

/*
 * Hardware pointers use one extra bit, which means that indexing FIFO and
 * values written into the pointers have to have different sizes. Tracked under
 * http://b/20894690
 */
#define SPS_FIFO_PTR_MASK	((SPS_FIFO_MASK << 1) | 1)

#define SPS_TX_FIFO_BASE_ADDR (GBASE(SPS) + 0x1000)
#define SPS_RX_FIFO_BASE_ADDR (SPS_TX_FIFO_BASE_ADDR + SPS_FIFO_SIZE)

/* SPS Statistic Counters */
static uint32_t sps_tx_count, sps_rx_count, tx_empty_count, max_rx_batch;

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SPS, outstr)
#define CPRINTS(format, args...) cprints(CC_SPS, format, ## args)

/* Flag indicating if there has been any data received while CS was asserted. */
static uint8_t seen_data;

void sps_tx_status(uint8_t byte)
{
	GREG32(SPS, DUMMY_WORD) = byte;
}

/*
 * Push data to the SPS TX FIFO
 * @param data Pointer to 8-bit data
 * @param data_size Number of bytes to transmit
 * @return : actual number of bytes placed into tx fifo
 */
int sps_transmit(uint8_t *data, size_t data_size)
{
	volatile uint32_t *sps_tx_fifo;
	uint32_t rptr;
	uint32_t wptr;
	uint32_t fifo_room;
	int bytes_sent;
	int inst = 0;

	if (GREAD_FIELD_I(SPS, inst, ISTATE, TXFIFO_EMPTY))
		tx_empty_count++; /* Inside packet this means underrun. */

	sps_tx_fifo = (volatile uint32_t *)SPS_TX_FIFO_BASE_ADDR;

	wptr = GREG32_I(SPS, inst, TXFIFO_WPTR);
	rptr = GREG32_I(SPS, inst, TXFIFO_RPTR);
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
				 * modify/write is required. Tracked under
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
		GREG32_I(SPS, inst, TXFIFO_WPTR) = wptr & SPS_FIFO_PTR_MASK;

		/* Make sure FIFO pointer wraps along with the index. */
		if (!(wptr & SPS_FIFO_MASK))
			sps_tx_fifo = (volatile uint32_t *)
				SPS_TX_FIFO_BASE_ADDR;
	}

	/*
	 * Start TX if necessary. This happens after FIFO is primed, which
	 * helps alleviate TX underrun problems but introduces delay before
	 * data starts coming out.
	 */
	if (!GREAD_FIELD(SPS, FIFO_CTRL, TXFIFO_EN))
		GWRITE_FIELD(SPS, FIFO_CTRL, TXFIFO_EN, 1);

	sps_tx_count += bytes_sent;
	return bytes_sent;
}

static int sps_cs_asserted(void)
{
	/*
	 * Read the current value on the SPS CS line and return the iversion
	 * of it (CS is active low).
	 */
	return !GREAD_FIELD(SPS, VAL, CSB);
}

/** Configure the data transmission format
 *
 *  @param mode Clock polarity and phase mode (0 - 3)
 *
 */
static void sps_configure(enum sps_mode mode, enum spi_clock_mode clk_mode,
			  unsigned rx_fifo_threshold)
{
	/* Disable All Interrupts */
	GREG32(SPS, ICTRL) = 0;

	GWRITE_FIELD(SPS, CTRL, MODE, mode);
	GWRITE_FIELD(SPS, CTRL, IDLE_LVL, 0);
	GWRITE_FIELD(SPS, CTRL, CPHA, clk_mode & 1);
	GWRITE_FIELD(SPS, CTRL, CPOL, (clk_mode >> 1) & 1);
	GWRITE_FIELD(SPS, CTRL, TXBITOR, 1); /* MSB first */
	GWRITE_FIELD(SPS, CTRL, RXBITOR, 1); /* MSB first */
	/* xfer 0xff when tx fifo is empty */
	GREG32(SPS, DUMMY_WORD) = GC_SPS_DUMMY_WORD_DEFAULT;

	/* [5,4,3]           [2,1,0]
	 * RX{DIS, EN, RST} TX{DIS, EN, RST}
	 */
	GREG32(SPS, FIFO_CTRL) = 0x9;

	/* wait for reset to self clear. */
	while (GREG32(SPS, FIFO_CTRL) & 9)
		;

	/* Do not enable TX FIFO until we have something to send. */
	GWRITE_FIELD(SPS, FIFO_CTRL, RXFIFO_EN, 1);

	GREG32(SPS, RXFIFO_THRESHOLD) = rx_fifo_threshold;

	GWRITE_FIELD(SPS, ICTRL, RXFIFO_LVL, 1);

	seen_data = 0;

	/* Use CS_DEASSERT to retrieve all remaining bytes from RX FIFO. */
	GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
	GWRITE_FIELD(SPS, ICTRL, CS_DEASSERT, 1);
}

/*
 * Register and unregister rx_handler. Side effects of registering the handler
 * is reinitializing the interface.
 */
static rx_handler_f sps_rx_handler;

int sps_register_rx_handler(enum sps_mode mode, rx_handler_f rx_handler,
			    unsigned rx_fifo_threshold)
{
	task_disable_irq(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR);
	task_disable_irq(GC_IRQNUM_SPS0_CS_DEASSERT_INTR);

	if (!rx_handler)
		return 0;

	if (!rx_fifo_threshold)
		rx_fifo_threshold = 8;  /* This is a sensible default. */
	sps_rx_handler = rx_handler;

	sps_configure(mode, SPI_CLOCK_MODE0, rx_fifo_threshold);
	task_enable_irq(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR);
	task_enable_irq(GC_IRQNUM_SPS0_CS_DEASSERT_INTR);

	return 0;
}

static void sps_init(void)
{
	/*
	 * Check to see if slave SPI interface is required by the board before
	 * initializing it. If SPI option is not set, then just return.
	 */
	if (!board_tpm_uses_spi())
		return;

	pmu_clock_en(PERIPH_SPS);

	/* The pinmux connections are preset, but we have to set IN/OUT */
	GWRITE_FIELD(PINMUX, DIOA2_CTL, IE, 1);	 /* SPS_MOSI */
	GWRITE_FIELD(PINMUX, DIOA6_CTL, IE, 1);	 /* SPS_CLK */
	GWRITE_FIELD(PINMUX, DIOA10_CTL, IE, 0); /* SPS_MISO */
	GWRITE_FIELD(PINMUX, DIOA12_CTL, IE, 1); /* SPS_CS_L */

	/* Allow SPS_CS_L to wake from sleep */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA12, 1);   /* enable powerdown exit */
	GWRITE_FIELD(PINMUX, EXITEDGE0, DIOA12, 1); /* edge sensitive */
	GWRITE_FIELD(PINMUX, EXITINV0, DIOA12, 1);  /* wake on low */
}
DECLARE_HOOK(HOOK_INIT, sps_init, HOOK_PRIO_DEFAULT);



/*****************************************************************************/
/* Interrupt handler stuff */

/*
 * Check how much data is available in RX FIFO and return pointer to the
 * available data and its size.
 *
 * @param inst Interface number
 * @param data - pointer to set to the beginning of data in the fifo
 * @return number of available bytes and the sets the pointer if number of
 *         bytes is non zero
 */
static int sps_check_rx(uint32_t inst, uint8_t **data)
{
	uint32_t write_ptr = GREG32_I(SPS, inst, RXFIFO_WPTR) & SPS_FIFO_MASK;
	uint32_t read_ptr = GREG32_I(SPS, inst, RXFIFO_RPTR) & SPS_FIFO_MASK;

	if (read_ptr == write_ptr)
		return 0;

	*data = (uint8_t *)(SPS_RX_FIFO_BASE_ADDR + read_ptr);

	if (read_ptr > write_ptr)
		return SPS_FIFO_SIZE - read_ptr;

	return write_ptr - read_ptr;
}

/* Advance RX FIFO read pointer after data has been read from the FIFO. */
static void sps_advance_rx(int port, int data_size)
{
	uint32_t read_ptr = GREG32_I(SPS, port, RXFIFO_RPTR) + data_size;

	GREG32_I(SPS, port, RXFIFO_RPTR) = read_ptr & SPS_FIFO_PTR_MASK;
}

/*
 * Actual receive interrupt processing function. Invokes the callback passing
 * it a pointer to the linear space in the RX FIFO and the number of bytes
 * available at that address.
 *
 * If RX fifo is wrapping around, the callback will be called twice with two
 * flat pointers.
 *
 * If the CS has been deasserted, after all remaining RX FIFO data has been
 * passed to the callback, the callback is called one last time with zero data
 * size and the CS indication, this allows the client to delineate received
 * packets.
 */
static void sps_rx_interrupt(uint32_t port, int cs_deasserted)
{
	for (;;) {
		uint8_t *received_data = NULL;
		size_t data_size;

		data_size = sps_check_rx(port, &received_data);
		if (!data_size)
			break;

		seen_data = 1;
		sps_rx_count += data_size;

		if (sps_rx_handler)
			sps_rx_handler(received_data, data_size, 0);

		if (data_size > max_rx_batch)
			max_rx_batch = data_size;

		sps_advance_rx(port, data_size);
	}

	if (cs_deasserted) {
		if (seen_data) {
			sps_rx_handler(NULL, 0, 1);

			/*
			 * Signal the AP that this SPI frame processing is
			 * completed.
			 */
			gpio_set_level(GPIO_INT_AP_L, 0);
			gpio_set_level(GPIO_INT_AP_L, 1);
			seen_data = 0;
		}
	}
}

static void sps_cs_deassert_interrupt(uint32_t port)
{
	if (sps_cs_asserted()) {
		/*
		 * we must have been slow, this is the next CS assertion after
		 * the 'wake up' pulse, but we have not processed the wake up
		 * interrupt yet.
		 *
		 * There would be no other out of order CS assertions, as all
		 * the 'real' ones (as opposed to the wake up pulses) are
		 * confirmed by the H1 pulsing the AP interrupt line
		 */

		/*
		 * Make sure we react to the next deassertion when it
		 * happens.
		 */
		GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
		GWRITE_FIELD(SPS, FIFO_CTRL, TXFIFO_EN, 0);
		if (sps_cs_asserted())
			return;

		/*
		 * The CS went away while we were processing this interrupt,
		 * this was the 'real' CS, need to process data.
		 */
	}

	/* Make sure the receive FIFO is drained. */
	sps_rx_interrupt(port, 1);
	GWRITE_FIELD(SPS, ISTATE_CLR, CS_DEASSERT, 1);
	GWRITE_FIELD(SPS, FIFO_CTRL, TXFIFO_EN, 0);

	/*
	 * And transmit FIFO is emptied, so the next transaction doesn't start
	 * by clocking out any bytes left over from this one.
	 */
	GREG32(SPS, TXFIFO_WPTR) = GREG32(SPS, TXFIFO_RPTR);
}

void _sps0_interrupt(void)
{
	sps_rx_interrupt(0, 0);
}

void _sps0_cs_deassert_interrupt(void)
{
	sps_cs_deassert_interrupt(0);
}
DECLARE_IRQ(GC_IRQNUM_SPS0_CS_DEASSERT_INTR, _sps0_cs_deassert_interrupt, 1);
DECLARE_IRQ(GC_IRQNUM_SPS0_RXFIFO_LVL_INTR, _sps0_interrupt, 1);

#ifdef CONFIG_SPS_TEST

/* Function to test SPS driver. It expects the host to send SPI frames of size
 * <size> (not exceeding 1100) of the following format:
 *
 * <size/256> <size%256> [<size> bytes of payload]
 *
 * Once the frame is received, it is sent back. The host can receive it and
 * compare with the original.
 */

 /*
  * Receive callback implements a simple state machine, it could be in one of
  * three states:  not started, receiving frame, frame finished.
  */

enum sps_test_rx_state {
	spstrx_not_started,
	spstrx_receiving,
	spstrx_finished
};

static enum sps_test_rx_state rx_state;
static uint8_t test_frame[1100]; /* Storage for the received frame. */
/*
 * To verify different alignment cases, the frame is saved in the buffer
 * starting with a certain offset (in range 0..3).
 */
static size_t frame_base;
/*
 * This is the index of the next location where received data will be added
 * to. Points to the end of the received frame once it has been pulled in.
 */
static size_t frame_index;

static void sps_receive_callback(uint8_t *data, size_t data_size, int cs_status)
{
	static size_t frame_size; /* Total size of the frame being received. */
	size_t to_go; /* Number of bytes still to receive. */

	if (rx_state == spstrx_not_started) {
		if (data_size < 2)
			return; /* Something went wrong.*/

		frame_size = data[0] * 256 + data[1] + 2;
		frame_base = (frame_base + 1) % 3;
		frame_index = frame_base;

		if ((frame_index + frame_size) <= sizeof(test_frame))
			/* Enter 'receiving frame' state. */
			rx_state = spstrx_receiving;
		else
			/*
			 * If we won't be able to receive this much, enter the
			 * 'frame finished' state.
			 */
			rx_state = spstrx_finished;
	}

	if (rx_state == spstrx_finished) {
		/*
		 * If CS was deasserted (transitioned to 1) - prepare to start
		 * receiving the next frame.
		 */
		if (cs_status)
			rx_state = spstrx_not_started;
		return;
	}

	if (frame_size > data_size)
		to_go = data_size;
	else
		to_go = frame_size;

	memcpy(test_frame + frame_index, data, to_go);
	frame_index += to_go;
	frame_size -= to_go;

	if (!frame_size)
		rx_state = spstrx_finished; /* Frame finished.*/
}

static int command_sps(int argc, char **argv)
{
	int count = 0;
	int target = 10; /* Expect 10 frames by default.*/
	char *e;

	sps_tx_status(GC_SPS_DUMMY_WORD_DEFAULT);

	rx_state = spstrx_not_started;
	sps_register_rx_handler(SPS_GENERIC_MODE, sps_receive_callback, 0);

	if (argc > 1) {
		target = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	while (count++ < target) {
		size_t transmitted;
		size_t to_go;
		size_t index;

		/* Wait for a frame to be received.*/
		while (rx_state != spstrx_finished) {
			watchdog_reload();
			usleep(10);
		}

		/* Transmit the frame back to the host.*/
		index = frame_base;
		to_go = frame_index - frame_base;
		do {
			if ((index == frame_base) && (to_go > 8)) {
				/*
				 * This is the first transmit attempt for this
				 * frame. Send a little just to prime the
				 * transmit FIFO.
				 */
				transmitted = sps_transmit
					(test_frame + index, 8);
			} else {
				transmitted = sps_transmit
					(test_frame + index, to_go);
			}
			index += transmitted;
			to_go -= transmitted;
		} while (to_go);

		/*
		 * Wait for receive state machine to transition out of 'frame
		 * finished' state.
		 */
		while (rx_state == spstrx_finished) {
			watchdog_reload();
			usleep(10);
		}
	}

	ccprintf("Processed %d frames\n", count - 1);
	ccprintf("rx count %d, tx count %d, tx_empty %d, max rx batch %d\n",
		 sps_rx_count, sps_tx_count,
		 tx_empty_count, max_rx_batch);

	sps_rx_count =
		sps_tx_count =
		tx_empty_count =
		max_rx_batch = 0;

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(spstest, command_sps,
			"<num of frames>",
			"Loop back frames (10 by default) back to the host");
#endif /* CONFIG_SPS_TEST */
