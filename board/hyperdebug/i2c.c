/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug I2C logic and console commands */

#include "cmsis-dap.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb-stream.h"
#include "usb_i2c.h"
#include "util.h"

/*
 * This file implements control of I2C interfaces via USB.
 *
 * Console commands are used to set speed, and to switch I2C ports in and out
 * of "device mode" (in which HyperDebug will respond when addressed.)
 *
 * Actual I2C data is tunnelled through CMSIS-DAP using one of two "vendor
 * extension" commands.
 *
 * Vendor extension 0x81 is used when HyperDebug is I2C host, and the header
 * byte is immediately followed by a request of the same format as described
 * in include/usb_i2c.h.
 *
 * Vendor extension 0x82 is used when HyperDebug is I2C device, and encodes a
 * number of request sub-types.
 *
 *
 * Get transcript request:
 * +----------------+---------------+--------------+-----------------+
 * | CMSIS req : 1B | I2C port : 1B | I2C req : 1B | timeout ms : 2B |
 * +----------------+---------------+--------------+-----------------+
 *
 * CMSIS req  : 0x81
 * I2C port   : range 0-15
 * I2C req    : 0x00
 * timeout ms : Response will be sent as soon as something is available, or
 *              when this amount of time has passed.
 *
 * Get transcript response:
 * +----------------+----------+------------------+-----------+----------+
 * | CMSIS req : 1B | off : 2B | read status : 1B | addr : 1B | len : 2B |
 * +----------------+----------+------------------+-----------+----------+
 *
 * CMSIS req   : 0x81
 * off         : bytes from start of this field to start of transcript data
 * read status : 0: No data prepared, no read in progress
 *               1: Data prepared, no read in progress
 *               2: No data prepared, read in progress (currently blocked
 *                  indefinitely)
 * addr        : Upper 7 bits contain I2C address of blocked read
 * len         : Length of transcript in bytes
 *
 * The above response header is followed by a "transcript", which consists of
 * zero or more entries each consisting of a four-byte header followed by a
 * number of data bytes padded to a multiple of 4 bytes.
 *
 * Transcript transfer header:
 * +-----------+------------+----------+
 * | addr : 1B | flags : 21 | len : 2B |
 * +-----------+------------+----------+
 *
 * addr  : Upper 7 bits contain I2C address
 *         Low bit: 0=write, 1=read
 * flags : Bitfield:
 *         0x01: Timeout, HyperDebug did not have response for I2C host
 * len   : Number of data bytes in transfer.
 *         For WRITE transfers (only) this header is followed by the actual
 *         data bytes padded to a multiple of 4 bytes.
 *
 *
 * Prepare read data:
 * +----------------+---------------+--------------+----------+------------+
 * | CMSIS req : 1B | I2C port : 1B | I2C req : 1B | len : 2B | data : len |
 * +----------------+---------------+--------------+----------+------------+
 *
 * CMSIS req  : 0x81
 * I2C port   : Bitfield:
 *              0x80: Sticky bit.  If set, the prepared response will be used,
 *                    even after intervening I2C write transfers.
 *              0x0F: I2C port number, range 0-15
 * I2C req    : 0x01
 * len        : Number of bytes of data to follow
 * data       : Response on next I2C read transfer
 *
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "I2C1",
	  .port = 0,
	  .kbps = 100,
	  .scl = GPIO_CN7_2,
	  .sda = GPIO_CN7_4,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "I2C2",
	  .port = 1,
	  .kbps = 100,
	  .scl = GPIO_CN9_19,
	  .sda = GPIO_CN9_21,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "I2C3",
	  .port = 2,
	  .kbps = 100,
	  .scl = GPIO_CN9_11,
	  .sda = GPIO_CN9_9,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "I2C4",
	  .port = 3,
	  .kbps = 100,
	  .scl = GPIO_CN10_8,
	  .sda = GPIO_CN10_12,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

struct i2c_state_t {
	/* Current clock speed setting used in I2C host mode. */
	uint32_t bits_per_second;

	/*
	 * Variable below used in I2C device mode.
	 */

	/*
	 * If prepared_read_len is nonzero, prepared_read_data contains data
	 * prepared to be sent on next I2C READ transfer.
	 */
	volatile size_t prepared_read_len;
	volatile bool prepared_read_sticky;
	uint8_t prepared_read_data[256];

	/*
	 * If non-zero, I2C host is currently waiting in READ transfer, with
	 * HyperDebug stretching the clock until receiving data to respond
	 * with.
	 */
	volatile uint8_t blocked_read_addr;

	/*
	 * Pointer to oldest record in data_buffer. If head == tail, then buffer
	 * is empty.
	 */
	const uint8_t *tail;
	/*
	 * Where to place the next record.  Data range between tail and head are
	 * valid transcript, ready to consume.
	 */
	uint8_t *volatile head;

	/*
	 * Variables used only by interrupt handler.
	 */
	uint8_t *byte_head;
	struct i2c_transfer_t *cur_transfer;

	/* Cyclic buffer recording transfers on the I2C bus */
	uint8_t data_buffer[4096] __attribute__((aligned(4)));
	;
};

const uint8_t I2C_REQ_GET_TRANSCRIPT = 0x00;
const uint8_t I2C_REQ_PREPARE_READ = 0x01;

/* Bitfield, Prepare read data request, I2C port field */
const uint8_t PREPARE_READ_FLAG_STICKY = BIT(7);
const uint8_t PREPARE_READ_PORT_MASK = 0x0F;

/* Bitfield, i2c_transfer_t, flags */
const uint8_t TRANSFER_FLAG_TIMEOUT = BIT(0);

/*
 * This header is used both on each transaction in the in-memory cyclic buffer,
 * as well as in the binary USB protocol.
 */
struct i2c_transfer_t {
	/* 7-bit address in high bits, low bit: 0=write, 1=read */
	uint8_t addr;
	uint8_t flags;
	/*
	 * Number of data bytes transferred.  In case of write transfer, this
	 * record is followed by the actual bytes, rounded up to a multiple of 4
	 * bytes, such that this record itself will never "wrap around" the
	 * cyclic buffer.
	 */
	uint16_t num_bytes;
};

/*
 * Declaration of header used in the binary USB protocol (Google HyperDebug
 * extensions to CMSIS-DAP protocol.)
 */
struct i2c_device_status_t {
	/* Size of this struct, including the size field. */
	uint16_t transcript_offset;
	/*
	 * 0: No data prepared, no read in progress
	 * 1: Data prepared, no read in progress
	 * 2: No data prepared, read in progress (currently blocked
	 * indefinitely)
	 */
	uint8_t read_status;
	/* Upper 7 bits indicate the address from which the I2C bus host wants
	 * to read. */
	uint8_t blocked_read_addr;
	/*
	 * Number of bytes of transcript following this struct.  Transcript
	 * consists of a number of `struct i2c_transfer_t` each followed by a
	 * number of data bytes, padded to a multiple of 4 bytes.
	 */
	uint16_t transcript_size;
};

static struct i2c_state_t i2c_port_state[ARRAY_SIZE(i2c_ports)];

/* All the interrupt flags normally enabled in device mode. */
static const uint32_t I2C_CR1_DEVICE_FLAGS =
	STM32_I2C_CR1_TXIE | STM32_I2C_CR1_RXIE | STM32_I2C_CR1_ADDRIE |
	STM32_I2C_CR1_NACKIE | STM32_I2C_CR1_STOPIE | STM32_I2C_CR1_ERRIE;

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/*
 * A few routines mostly copied from usb_i2c.c.
 */

static int16_t usb_i2c_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:
		return USB_I2C_SUCCESS;
	case EC_ERROR_TIMEOUT:
		return USB_I2C_TIMEOUT;
	case EC_ERROR_BUSY:
		return USB_I2C_BUSY;
	default:
		return USB_I2C_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

static void usb_i2c_execute(unsigned int expected_size)
{
	uint32_t count = queue_remove_units(&cmsis_dap_rx_queue, rx_buffer,
					    expected_size + 1) -
			 1;
	uint16_t i2c_status = 0;
	/* Payload is ready to execute. */
	int portindex = rx_buffer[1] & 0xf;
	uint16_t addr_flags = rx_buffer[2] & 0x7f;
	int write_count = ((rx_buffer[1] << 4) & 0xf00) | rx_buffer[3];
	int read_count = rx_buffer[4];
	int offset = 0; /* Offset for extended reading header. */

	rx_buffer[1] = 0;
	rx_buffer[2] = 0;
	rx_buffer[3] = 0;
	rx_buffer[4] = 0;

	if (read_count & 0x80) {
		read_count = (rx_buffer[5] << 7) | (read_count & 0x7f);
		offset = 2;
	}

	if (!usb_i2c_board_is_enabled()) {
		i2c_status = USB_I2C_DISABLED;
	} else if (!read_count && !write_count) {
		/* No-op, report as success */
		i2c_status = USB_I2C_SUCCESS;
	} else if (write_count > CONFIG_USB_I2C_MAX_WRITE_COUNT ||
		   write_count != (count - 4 - offset)) {
		i2c_status = USB_I2C_WRITE_COUNT_INVALID;
	} else if (read_count > CONFIG_USB_I2C_MAX_READ_COUNT) {
		i2c_status = USB_I2C_READ_COUNT_INVALID;
	} else if (portindex >= i2c_ports_used) {
		i2c_status = USB_I2C_PORT_INVALID;
	} else {
		int ret = i2c_xfer(i2c_ports[portindex].port, addr_flags,
				   rx_buffer + 5 + offset, write_count,
				   rx_buffer + 5, read_count);
		i2c_status = usb_i2c_map_error(ret);
	}
	rx_buffer[1] = i2c_status & 0xFF;
	rx_buffer[2] = i2c_status >> 8;
	/*
	 * Send one byte of CMSIS-DAP header, four bytes of Google I2C header,
	 * followed by any data received via I2C.
	 */
	queue_add_units(&cmsis_dap_tx_queue, rx_buffer, 1 + 4 + read_count);
}

/*
 * Entry point for CMSIS-DAP vendor command for I2C forwarding.
 */
void dap_goog_i2c(size_t peek_c)
{
	unsigned int expected_size;

	if (peek_c < 5)
		return;

	/*
	 * The first four bytes of the packet (following the CMSIS-DAP one-byte
	 * header) will describe its expected size.
	 */
	if (rx_buffer[4] & 0x80)
		expected_size = 6;
	else
		expected_size = 4;

	/* write count */
	expected_size += (((size_t)rx_buffer[1] & 0xf0) << 4) | rx_buffer[3];

	if (queue_count(&cmsis_dap_rx_queue) >= expected_size + 1) {
		usb_i2c_execute(expected_size);
	}
}

/*
 * Entry point for CMSIS-DAP vendor command for I2C device control.
 */
void dap_goog_i2c_device(size_t peek_c)
{
	if (peek_c < 3)
		return;

	uint8_t index = rx_buffer[1] & PREPARE_READ_PORT_MASK;
	struct i2c_state_t *state = &i2c_port_state[index];

	switch (rx_buffer[2]) {
	case I2C_REQ_GET_TRANSCRIPT: {
		if (peek_c < 5)
			return;
		uint16_t timeout_ms = rx_buffer[3] + (rx_buffer[4] << 8);
		timestamp_t deadline = get_time();
		deadline.val += timeout_ms * MSEC;

		queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 5);

		int64_t remaining = deadline.val - get_time().val;
		while (remaining > 0) {
			if (state->cur_transfer) {
				/* Ongoing transaction, wait to include that
				 * in report. */
			} else if (state->blocked_read_addr ||
				   state->head != state->tail) {
				/* Something to report already. */
				break;
			}
			/* Nothing to report, yet, wait. */
			task_wait_event(remaining);
			remaining = deadline.val - get_time().val;
		}

		/*
		 * Inspect state->block_read_addr BEFORE state->head (both
		 * volatile), to make sure that if the I2C host issues a write
		 * followed by read, that we cannot see and report the blocked
		 * read without also seeing and reporting the preceding
		 * write.
		 */
		struct i2c_device_status_t *status =
			(struct i2c_device_status_t *)(rx_buffer + 1);
		status->transcript_offset = sizeof(*status);
		if (state->blocked_read_addr) {
			status->read_status = 2;
			status->blocked_read_addr = state->blocked_read_addr;
		} else {
			status->read_status = state->prepared_read_len ? 1 : 0;
			status->blocked_read_addr = 0;
		}
		const uint8_t *head = state->head;
		if (state->tail <= head) {
			/* One contiguous range */
			status->transcript_size = head - state->tail;
			queue_add_units(&cmsis_dap_tx_queue, rx_buffer,
					1 + sizeof(*status));
			queue_add_units(&cmsis_dap_tx_queue, state->tail,
					status->transcript_size);
		} else {
			/* Data wraps around */
			status->transcript_size =
				head - state->tail + sizeof(state->data_buffer);
			queue_add_units(&cmsis_dap_tx_queue, rx_buffer,
					1 + sizeof(*status));
			queue_add_units(&cmsis_dap_tx_queue, state->tail,
					state->data_buffer +
						sizeof(state->data_buffer) -
						state->tail);
			queue_add_units(&cmsis_dap_tx_queue, state->data_buffer,
					state->head - state->data_buffer);
		}
		state->tail = head;
		return;
	}
	case I2C_REQ_PREPARE_READ:
		if (peek_c < 5)
			return;
		bool sticky = rx_buffer[1] & PREPARE_READ_FLAG_STICKY;
		uint16_t len = rx_buffer[3] + (rx_buffer[4] << 8);
		/* TODO Check that len does not exceed size of
		 * prepared_data_data */
		queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 5);
		queue_remove_units(&cmsis_dap_rx_queue,
				   state->prepared_read_data, len);
		queue_add_unit(&cmsis_dap_tx_queue, rx_buffer);
		state->prepared_read_len = len;
		state->prepared_read_sticky = sticky;
		if (!state->blocked_read_addr)
			return;

		/*
		 * I2C bus is currently awaiting ACK on our address, READ
		 * transfer.
		 */
		state->blocked_read_addr = 0;

		/*
		 * Ack previous START condition, will unblock clock
		 * stretching.
		 */
		STM32_I2C_ICR(index) = STM32_I2C_ICR_ADDRCF;
		/* Re-enable interrupt on START condition, for next
		 * transfer. */
		STM32_I2C_CR1(index) |= STM32_I2C_CR1_ADDRIE;

		/*
		 * At this point, we have pulled down SDA to ack our address,
		 * and released SCL to allow the I2C host to continue clocking
		 * bits.
		 *
		 * As soon as the host generates a falling edge on SCL, an
		 * I2C_ISR_TXIS interrupt will be triggered, as the hardware
		 * needs the first data byte to send.  If the I2C host has
		 * given up on getting the data, no such falling edge will be
		 * observed.  To avoid us keeping SDA low forever in such
		 * case, we stay in a busy loop below, waiting to observe the
		 * handling of the aforementioned interrupt.
		 */

		/* Wait up to one bit time, plus a buffer of 100us */
		uint32_t timeout_us = 100 + 1000000 / state->bits_per_second;

		volatile uint16_t *num_bytes = &state->cur_transfer->num_bytes;
		timestamp_t start_time = get_time();
		do {
			if (*num_bytes != 0)
				return;
		} while (get_time().val - start_time.val < timeout_us);

		/*
		 * I2C host did not generate falling edge, must have given up.
		 * Reset our I2C peripheral to release SDA (effectively
		 * generating stop condition), and record a the read transfer
		 * as "timed out" for the transcript.
		 */
		STM32_I2C_CR1(index) = 0;
		state->cur_transfer->flags |= TRANSFER_FLAG_TIMEOUT;
		state->prepared_read_len = 0;
		state->head = state->byte_head;
		state->cur_transfer = NULL;
		STM32_I2C_CR1(index) = STM32_I2C_CR1_PE | I2C_CR1_DEVICE_FLAGS;
		return;
	}
}

/*
 * I2C hardware interrupt, used only for I2C device mode.
 */
static void i2c_interrupt(int index)
{
	uint32_t isr = STM32_I2C_ISR(index);
	struct i2c_state_t *state = &i2c_port_state[index];

	if ((isr & STM32_I2C_ISR_ADDR) &&
	    (STM32_I2C_CR1(index) & STM32_I2C_CR1_ADDRIE)) {
		if (state->cur_transfer) {
			/* Record previous transaction */
			if (!(state->cur_transfer->addr & 0x01)) {
				/* Write transaction, pad data to align by 4
				 * bytes. */
				state->byte_head +=
					-state->cur_transfer->num_bytes & 0x03;
				if (state->byte_head ==
				    state->data_buffer +
					    sizeof(state->data_buffer))
					state->byte_head = state->data_buffer;
			} else {
				state->prepared_read_len = 0;
			}
			state->head = state->byte_head;

			/*
			 * In case a thread is waiting for non-empty transcript
			 * in dap_goog_i2c_device(), wake it up.
			 */
			task_wake(TASK_ID_CMSIS_DAP);
		}

		state->cur_transfer = (struct i2c_transfer_t *)state->head;
		state->cur_transfer->addr = (isr >> 16) & 0xFF;
		state->cur_transfer->flags = 0;
		state->cur_transfer->num_bytes = 0;
		state->byte_head = state->head + 4;
		if (state->byte_head ==
		    state->data_buffer + sizeof(state->data_buffer))
			state->byte_head = state->data_buffer;
		if (isr & STM32_I2C_ISR_DIR) {
			/* Read transfer */
			if (state->prepared_read_len) {
				/* We have data to respond */
				STM32_I2C_ICR(index) = STM32_I2C_ICR_ADDRCF;
			} else {
				/* No response, stretch clock */
				state->blocked_read_addr = (isr >> 16) & 0xFF;
				STM32_I2C_CR1(index) &= ~STM32_I2C_CR1_ADDRIE;

				/*
				 * In case a thread is waiting for a pending I2C
				 * read transfer in dap_goog_i2c_device(), wake
				 * it up.
				 */
				task_wake(TASK_ID_CMSIS_DAP);
			}
		} else {
			/* Write transfer */
			STM32_I2C_ICR(index) = STM32_I2C_ICR_ADDRCF;
			if (state->prepared_read_len > 0 &&
			    !state->prepared_read_sticky) {
				/* Discard any prepared data, may be stale. */
				state->prepared_read_len = 0;
			}
		}
	}
	if (isr & STM32_I2C_ISR_RXNE) {
		*state->byte_head++ = STM32_I2C_RXDR(index);
		state->cur_transfer->num_bytes++;
		if (state->byte_head ==
		    state->data_buffer + sizeof(state->data_buffer))
			state->byte_head = state->data_buffer;
	}
	if (isr & STM32_I2C_ISR_TIMEOUT) {
		/*
		 * Clock stretch timeout.  This means that prepared_read_data
		 * was not provided in time to respond to the I2C host.  By the
		 * time control enters this handler, the hardware will already
		 * have released SCL letting it go high, while pulling SDA low
		 * to ACK.  Resetting the I2C peripheral below will release SDA
		 * to go high, while SCL is high, in effect generating a STOP
		 * condition on the I2C bus (not that it matters much, since the
		 * I2C host has probably given up a long time ago).
		 */
		STM32_I2C_CR1(index) = 0;
		if (state->cur_transfer == NULL) {
			panic("Timeout without cur_transfer?");
		}
		if (!(state->cur_transfer->addr & 0x01)) {
			panic("Timeout on I2C write?");
		}
		state->cur_transfer->flags |= TRANSFER_FLAG_TIMEOUT;
		state->prepared_read_len = 0;
		state->blocked_read_addr = 0;
		state->head = state->byte_head;
		state->cur_transfer = NULL;
		STM32_I2C_CR1(index) = STM32_I2C_CR1_PE | I2C_CR1_DEVICE_FLAGS;

		/*
		 * In case a thread is waiting for non-empty transcript in
		 * dap_goog_i2c_device(), wake it up.
		 */
		task_wake(TASK_ID_CMSIS_DAP);

		/*
		 * Peripheral has been reset, no sense in handling other
		 * interrupts from before it was reset.
		 */
		return;
	}
	if (isr & STM32_I2C_ISR_TXIS) {
		if (state->cur_transfer->num_bytes >=
		    state->prepared_read_len) {
			STM32_I2C_TXDR(index) = 0xFF;
		} else {
			uint8_t data_byte =
				state->prepared_read_data[state->cur_transfer
								  ->num_bytes];
			STM32_I2C_TXDR(index) = data_byte;
		}
		state->cur_transfer->num_bytes++;
	}
	if (isr & STM32_I2C_ISR_NACK) {
		state->cur_transfer->num_bytes--;
		STM32_I2C_ICR(index) = STM32_I2C_ICR_NACKCF;
	}
	if (isr & STM32_I2C_ISR_STOP) {
		STM32_I2C_ICR(index) = STM32_I2C_ICR_STOPCF;
		STM32_I2C_ISR(index) = STM32_I2C_ISR_TXE;
		if (!(state->cur_transfer->addr & 0x01)) {
			/* Write transaction, pad data to align by 4 bytes. */
			state->byte_head += -state->cur_transfer->num_bytes &
					    0x03;
			if (state->byte_head ==
			    state->data_buffer + sizeof(state->data_buffer))
				state->byte_head = state->data_buffer;
		} else {
			state->prepared_read_len = 0;
		}
		state->head = state->byte_head;
		state->cur_transfer = NULL;

		/*
		 * In case a thread is waiting for non-empty transcript in
		 * dap_goog_i2c_device(), wake it up.
		 */
		task_wake(TASK_ID_CMSIS_DAP);
	}
}

static void i2c_interrupt_i2c1(void)
{
	i2c_interrupt(0); /* zero-based index */
}

static void i2c_interrupt_i2c2(void)
{
	i2c_interrupt(1); /* zero-based index */
}

static void i2c_interrupt_i2c3(void)
{
	i2c_interrupt(2); /* zero-based index */
}

static void i2c_interrupt_i2c4(void)
{
	i2c_interrupt(3); /* zero-based index */
}

DECLARE_IRQ(STM32_IRQ_I2C1_EV, i2c_interrupt_i2c1, 1);
DECLARE_IRQ(STM32_IRQ_I2C2_EV, i2c_interrupt_i2c2, 1);
DECLARE_IRQ(STM32_IRQ_I2C3_EV, i2c_interrupt_i2c3, 1);
DECLARE_IRQ(STM32_IRQ_I2C4_EV, i2c_interrupt_i2c4, 1);
DECLARE_IRQ(STM32_IRQ_I2C1_ER, i2c_interrupt_i2c1, 1);
DECLARE_IRQ(STM32_IRQ_I2C2_ER, i2c_interrupt_i2c2, 1);
DECLARE_IRQ(STM32_IRQ_I2C3_ER, i2c_interrupt_i2c3, 1);
DECLARE_IRQ(STM32_IRQ_I2C4_ER, i2c_interrupt_i2c4, 1);

/*
 * Find i2c port by name or by number.  Returns an index into i2c_ports[], or on
 * error a negative value.
 */
static int find_i2c_by_name(const char *name)
{
	int i;
	char *e;
	i = strtoi(name, &e, 0);

	if (!*e && i < i2c_ports_used)
		return i;

	for (i = 0; i < i2c_ports_used; i++) {
		if (!strcasecmp(name, i2c_ports[i].name))
			return i;
	}

	/* I2C device not found */
	return -1;
}

static void print_i2c_info(int index)
{
	bool is_device = STM32_I2C_OAR1(index) & 0x8000;

	ccprintf("  %d %s %d bps %s\n", index, i2c_ports[index].name,
		 i2c_port_state[index].bits_per_second, is_device ? "d" : "h");

	/* Flush console to avoid truncating output */
	cflush();
}

/*
 * Get information about one or all I2C ports.
 */
static int command_i2c_info(int argc, const char **argv)
{
	int i;

	/* If a I2C port is specified, print only that one */
	if (argc == 3) {
		int index = find_i2c_by_name(argv[2]);
		if (index < 0) {
			ccprintf("I2C port not found\n");
			return EC_ERROR_PARAM2;
		}

		print_i2c_info(index);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0; i < i2c_ports_used; i++) {
		print_i2c_info(i);
	}

	return EC_SUCCESS;
}

/*
 * Constants copied from i2c-stm32l4.c, for 16MHz base frequency.
 */
static const uint32_t TIMINGR_I2C_FREQ_1000KHZ = 0x00000107;
static const uint32_t TIMINGR_I2C_FREQ_400KHZ = 0x00100B15;
static const uint32_t TIMINGR_I2C_FREQ_100KHZ = 0x00303D5B;

/*
 * This function builds on a similar function in i2c-stm32l4.c, adding support
 * for non-standard speeds slower than 100kbps.
 */
static void board_i2c_set_speed(int port, uint32_t desired_speed)
{
	i2c_lock(port, 1);

	/* Disable port */
	STM32_I2C_CR1(port) = 0;

	if (desired_speed >= 1000000) {
		/* Set clock frequency */
		STM32_I2C_TIMINGR(port) = TIMINGR_I2C_FREQ_1000KHZ;
		i2c_port_state[port].bits_per_second = 1000000;
	} else if (desired_speed >= 400000) {
		STM32_I2C_TIMINGR(port) = TIMINGR_I2C_FREQ_400KHZ;
		i2c_port_state[port].bits_per_second = 400000;
	} else {
		/*
		 * The code below uses the above constant meant for 100kbps I2C
		 * clock, and possibly modifies the prescaling value, to divide
		 * the frequency with an integer in the range 1-16.  It will
		 * find the closest I2C frequency in the range 6.25kbps -
		 * 100kbps which is not faster than the requested speed, except
		 * if the requested speed is slower than the slowest supported
		 * value.
		 */
		int divisor = 100000 / (desired_speed + 1);
		if (divisor > 15)
			divisor = 15;
		STM32_I2C_TIMINGR(port) = TIMINGR_I2C_FREQ_100KHZ |
					  (divisor << 28);
		i2c_port_state[port].bits_per_second = 100000 / (divisor + 1);
	}

	/* Enable port */
	STM32_I2C_CR1(port) = STM32_I2C_CR1_PE;

	i2c_lock(port, 0);
}

static int command_i2c_set_speed(int argc, const char **argv)
{
	int index;
	uint32_t desired_speed;
	char *e;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_i2c_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	desired_speed = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	board_i2c_set_speed(index, desired_speed);

	return EC_SUCCESS;
}

static int command_i2c_set_mode(int argc, const char **argv)
{
	int index;
	const char *mode;
	int i2c_addr;
	char *e;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_i2c_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	mode = argv[4];
	if (!strcasecmp(mode, "host")) {
		STM32_I2C_CR1(index) = STM32_I2C_CR1_PE;
		STM32_I2C_OAR1(index) = 0;
	} else if (!strcasecmp(mode, "device")) {
		if (argc < 6)
			return EC_ERROR_PARAM_COUNT;
		i2c_addr = strtoi(argv[5], &e, 0);
		if (*e)
			return EC_ERROR_PARAM5;
		STM32_I2C_CR1(index) = STM32_I2C_CR1_PE | I2C_CR1_DEVICE_FLAGS;
		STM32_I2C_TIMEOUTR(index) = 0x00008FFF;
		/*
		 * "Own address" cannot be modified while active, so first
		 * disable, then set desired address while enabling, for the
		 * case the bus was already in device mode with another address.
		 */
		STM32_I2C_OAR1(index) = 0;
		STM32_I2C_OAR1(index) = 0x8000 | (i2c_addr << 1);
	} else {
		return EC_ERROR_PARAM4;
	}

	return EC_SUCCESS;
}

static int command_i2c_set(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "speed"))
		return command_i2c_set_speed(argc, argv);
	if (!strcasecmp(argv[2], "mode"))
		return command_i2c_set_mode(argc, argv);
	return EC_ERROR_PARAM2;
}

static int command_i2c(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "info"))
		return command_i2c_info(argc, argv);
	if (!strcasecmp(argv[1], "set"))
		return command_i2c_set(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(i2c, command_i2c,
			      "info [PORT]"
			      "\nset speed PORT BPS",
			      "I2C bus manipulation", CMD_FLAG_RESTRICTED);

/* Reconfigure I2C ports to power-on default values. */
static void i2c_reinit(void)
{
	for (unsigned int i = 0; i < i2c_ports_used; i++) {
		board_i2c_set_speed(i, i2c_ports[i].kbps * 1000);
		i2c_port_state[i].bits_per_second = i2c_ports[i].kbps * 1000;
		/* Switch to host-only mode. */
		STM32_I2C_CR1(i) = STM32_I2C_CR1_PE;
		STM32_I2C_OAR1(i) = 0;
	}
}
DECLARE_HOOK(HOOK_REINIT, i2c_reinit, HOOK_PRIO_DEFAULT);

static void board_i2c_init(void)
{
	task_enable_irq(STM32_IRQ_I2C1_EV);
	task_enable_irq(STM32_IRQ_I2C2_EV);
	task_enable_irq(STM32_IRQ_I2C3_EV);
	task_enable_irq(STM32_IRQ_I2C4_EV);
	task_enable_irq(STM32_IRQ_I2C1_ER);
	task_enable_irq(STM32_IRQ_I2C2_ER);
	task_enable_irq(STM32_IRQ_I2C3_ER);
	task_enable_irq(STM32_IRQ_I2C4_ER);
	for (unsigned int i = 0; i < i2c_ports_used; i++) {
		i2c_port_state[i].bits_per_second = i2c_ports[i].kbps * 1000;
		i2c_port_state[i].prepared_read_len = 0;
		i2c_port_state[i].blocked_read_addr = 0;
		i2c_port_state[i].cur_transfer = NULL;
		i2c_port_state[i].tail = i2c_port_state[i].head =
			i2c_port_state[i].data_buffer;
	}
}
DECLARE_HOOK(HOOK_INIT, board_i2c_init, HOOK_PRIO_DEFAULT + 2);
