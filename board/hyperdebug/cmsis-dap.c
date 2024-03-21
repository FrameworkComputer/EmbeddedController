/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cmsis-dap.h"
#include "common.h"
#include "consumer.h"
#include "gpio.h"
#include "producer.h"
#include "queue.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb-stream.h"

static const uint32_t DEFAULT_JTAG_CLOCK_HZ = 100000;
static const uint32_t OVERHEAD_CLOCK_CYCLES = 50;

/*
 * The CMSIS-DAP specification calls for identifying the USB interface by
 * looking for "CMSIS-DAP" in the string name, not by subclass/protocol.
 */
#define USB_SUBCLASS_CMSIS_DAP 0x00
#define USB_PROTOCOL_CMSIS_DAP 0x00

/* CMSIS-DAP command bytes */
enum cmsis_dap_command_t {
	/* General commands */
	DAP_Info = 0x00,
	DAP_HostStatus = 0x01,
	DAP_Connect = 0x02,
	DAP_Disconnect = 0x03,
	DAP_TransferConfigure = 0x04,
	DAP_Transfer = 0x05,
	DAP_TransferBlock = 0x06,
	DAP_TransferAbort = 0x07,
	DAP_WriteAbort = 0x08,
	DAP_Delay = 0x09,
	DAP_ResetTarget = 0x0A,

	/* Commands used both for SWD and JTAG */
	DAP_SWJ_Pins = 0x10,
	DAP_SWJ_Clock = 0x11,
	DAP_SWJ_Sequence = 0x12,

	/* Commands used only with SWD */
	DAP_SWD_Configure = 0x13,

	/* Commands used only with JTAG */
	DAP_JTAG_Sequence = 0x14,
	DAP_JTAG_Configure = 0x15,
	DAP_JTAG_IdCode = 0x16,

	/* Commands used for UART tunnelling */
	DAP_SWO_Transport = 0x17,
	DAP_SWO_Mode = 0x18,
	DAP_SWO_Baudrate = 0x19,
	DAP_SWO_Control = 0x1A,
	DAP_SWO_Status = 0x1B,
	DAP_SWO_Data = 0x1C,

	/* Commands used to group other commands */
	DAP_QueueCommands = 0x7E,
	DAP_ExecuteCommands = 0x7F,

	/* Vendor-specific commands (reserved range 0x80 - 0x9F) */
	DAP_GOOG_Info = 0x80,
	DAP_GOOG_I2c = 0x81,
	DAP_GOOG_I2cDevice = 0x82,
	DAP_GOOG_Gpio = 0x83,
};

/* DAP Status Code */
enum cmsis_dap_status_t {
	STATUS_Ok = 0x00,
	STATUS_Error = 0xFF,
};

/* Parameter for info command */
enum cmsis_dap_info_subcommand_t {
	INFO_Vendor = 0x01,
	INFO_Product = 0x02,
	INFO_Serial = 0x03,
	INFO_Version = 0x04,
	INFO_DeviceVendor = 0x05,
	INFO_DeviceName = 0x06,
	INFO_Capabilities = 0xF0,
	INFO_SwoBufferSize = 0xFD,
	INFO_PacketCount = 0xFE,
	INFO_PacketSize = 0xFF,
};

/* Bitfield response to INFO_Capabilities */
const uint16_t CAP_Swd = BIT(0);
const uint16_t CAP_Jtag = BIT(1);
const uint16_t CAP_SwoUart = BIT(2);
const uint16_t CAP_SwoManchester = BIT(3);
const uint16_t CAP_AtomicCommands = BIT(4);
const uint16_t CAP_TestDomainTimer = BIT(5);
const uint16_t CAP_SwoStreamingTrace = BIT(6);
const uint16_t CAP_UartCommunicationPort = BIT(7);
const uint16_t CAP_UsbComPort = BIT(8);

enum connect_req_t {
	CONN_REQ_Default = 0,
	CONN_REQ_Swd = 1,
	CONN_REQ_Jtag = 2,
};

enum connect_resp_t {
	CONN_RESP_Failed = 0,
	CONN_RESP_Swd = 1,
	CONN_RESP_Jtag = 2,
};

/* Parameter for vendor (Google) info command */
enum goog_info_subcommand_t {
	GOOG_INFO_Capabilities = 0x00,
};

/* Bitfield response to vendor (Google) capabities request */
const uint32_t GOOG_CAP_I2c = BIT(0);
const uint32_t GOOG_CAP_I2cDevice = BIT(1);
const uint32_t GOOG_CAP_GpioMonitoring = BIT(2);
const uint32_t GOOG_CAP_GpioBitbanging = BIT(3);

/* Bitfield used in DAP_SWJ_Pins request */
const uint8_t PIN_SwClk_Tck = 0x01;
const uint8_t PIN_SwDio_Tms = 0x02;
const uint8_t PIN_Tdi = 0x04;
const uint8_t PIN_Tdo = 0x08;
const uint8_t PIN_Trst = 0x20;
const uint8_t PIN_Reset = 0x80;

/* Bitfield used in DAP_JTAG_Sequence request */
const uint8_t SEQ_NumBits = 0x3F;
const uint8_t SEQ_Tms = 0x40;
const uint8_t SEQ_CaptureTdo = 0x80;

/*
 * Incoming and outgoing byte streams.
 */

struct queue const cmsis_dap_tx_queue;
struct queue const cmsis_dap_rx_queue;

uint8_t rx_buffer[256];
uint8_t tx_buffer[256];

/*
 * JTAG state
 */
enum jtag_signal_t {
	JTAG_TCLK = 0,
	JTAG_TMS,
	JTAG_TDI,
	JTAG_TDO,
	JTAG_TRSTn,
	JTAG_INVALID
};

static int jtag_pins[JTAG_INVALID] = {
	GPIO_CN7_1, /* TCLK */
	GPIO_CN7_7, /* TMS */
	GPIO_CN7_3, /* TDI */
	GPIO_CN7_5, /* TDO */
	GPIO_CN7_16, /* TRSTn */
};
static int saved_pin_flags[JTAG_INVALID];
static bool jtag_enabled = false;
static uint16_t jtag_half_period_count =
	CPU_CLOCK / DEFAULT_JTAG_CLOCK_HZ / 2 - OVERHEAD_CLOCK_CYCLES;

void queue_blocking_add(struct queue const *q, const void *src, size_t count)
{
	while (true) {
		size_t progress = queue_add_units(q, src, count);
		src += progress;
		if (progress >= count)
			return;
		count -= progress;
		/*
		 * Wait for queue consumer to wake up this task, when there is
		 * more room in the queue.
		 */
		task_wait_event(0);
	}
}

void queue_blocking_remove(struct queue const *q, void *dest, size_t count)
{
	while (true) {
		size_t progress = queue_remove_units(q, dest, count);
		dest += progress;
		if (progress >= count)
			return;
		count -= progress;
		/*
		 * Wait for queue consumer to wake up this task, when there is
		 * more data in the queue.
		 */
		task_wait_event(0);
	}
}

/*
 * Implementation of handler routines for each CMSIS-DAP command.
 */

/* Info command, used to discover which other commands are supported. */
static void dap_info(size_t peek_c)
{
	const char *CMSIS_DAP_VERSION_STR = "2.1.1";
	const uint16_t CAPABILITIES = CAP_Jtag;
	struct usb_string_desc *sd = usb_serialno_desc;
	int i;

	if (peek_c < 2)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 2);
	switch (rx_buffer[1]) {
	case INFO_Serial:
		for (i = 0; i < CONFIG_SERIALNO_LEN && sd->_data[i]; i++)
			tx_buffer[2 + i] = sd->_data[i];
		tx_buffer[1] = i;
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2 + i);
		break;
	case INFO_Version:
		tx_buffer[1] = strlen(CMSIS_DAP_VERSION_STR) + 1;
		memcpy(tx_buffer + 2, CMSIS_DAP_VERSION_STR, tx_buffer[1]);
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer,
				2 + tx_buffer[1]);
		break;
	case INFO_Capabilities:
		tx_buffer[1] = sizeof(CAPABILITIES);
		memcpy(tx_buffer + 2, &CAPABILITIES, sizeof(CAPABILITIES));
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer,
				2 + tx_buffer[1]);
		break;
	default:
		tx_buffer[1] = 0;
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
		break;
	}
}

/* Informational command, to allow debugging device to indicate status. */
static void dap_host_status(size_t peek_c)
{
	if (peek_c < 3)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 3);
	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Establish JTAG connection, take control of JTAG pins. */
static void dap_connect(size_t peek_c)
{
	if (peek_c < 2)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 2);
	switch (rx_buffer[1]) {
	case CONN_REQ_Default:
	case CONN_REQ_Jtag:
		tx_buffer[1] = CONN_RESP_Jtag;
		if (!jtag_enabled) {
			jtag_enabled = true;
			for (size_t i = 0; i < JTAG_INVALID; i++) {
				saved_pin_flags[i] =
					gpio_get_flags(jtag_pins[i]);
			}

			gpio_set_flags(jtag_pins[JTAG_TMS], GPIO_OUT_LOW);
			gpio_set_flags(jtag_pins[JTAG_TDI], GPIO_OUT_LOW);
			gpio_set_flags(jtag_pins[JTAG_TCLK], GPIO_OUT_LOW);
			gpio_set_flags(jtag_pins[JTAG_TRSTn],
				       GPIO_ODR_HIGH | GPIO_PULL_UP);
			gpio_set_flags(jtag_pins[JTAG_TDO],
				       GPIO_INPUT | GPIO_PULL_UP);
		}
		break;
	default:
		tx_buffer[1] = CONN_RESP_Failed;
	}
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Restore JTAG pins to previous configuration. */
static void dap_disconnect(size_t peek_c)
{
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 1);

	if (jtag_enabled) {
		jtag_enabled = false;
		for (size_t i = 0; i < JTAG_INVALID; i++) {
			gpio_set_flags(jtag_pins[i], saved_pin_flags[i]);
		}
	}

	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Configure parameters for DAP_Transfer family of requests. */
static void dap_transfer_configure(size_t peek_c)
{
	if (peek_c < 6)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 6);

	/*
	 * This file does not offer support for the DAP_Transfer family of
	 * requests, and OpenOCD does not seem to issue any requests (at least
	 * not when operating on a RISC-V OpenTitan code.
	 *
	 * OpenOCD still sends this configuration request as part of its setup
	 * sequence, we can safely ignore the parameters given, and report
	 * success to the caller.
	 */

	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Reset the GSC (using same pin as if blue button was pressed). */
static void dap_reset_target(size_t peek_c)
{
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 1);

	if (shield_reset_pin != GPIO_COUNT) {
		gpio_set_level(shield_reset_pin, false);
		usleep(100000);
		gpio_set_level(shield_reset_pin, true);
		tx_buffer[2] = 1;
	} else {
		tx_buffer[2] = 0;
	}
	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 3);
}

/* One-time setting of the output level of each JTAG signal. */
static void dap_swj_pins(size_t peek_c)
{
	if (peek_c < 7)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 7);

	uint8_t pin_value = rx_buffer[1];
	uint8_t pin_mask = rx_buffer[2];
	uint32_t wait_us;
	memcpy(&wait_us, rx_buffer + 3, sizeof(wait_us));

	if ((pin_mask & PIN_SwClk_Tck))
		gpio_set_level(jtag_pins[JTAG_TCLK],
			       !!(pin_value & PIN_SwClk_Tck));
	if ((pin_mask & PIN_SwDio_Tms))
		gpio_set_level(jtag_pins[JTAG_TMS],
			       !!(pin_value & PIN_SwDio_Tms));
	if ((pin_mask & PIN_Tdi))
		gpio_set_level(jtag_pins[JTAG_TDI], !!(pin_value & PIN_Tdi));
	if ((pin_mask & PIN_Trst))
		gpio_set_level(jtag_pins[JTAG_TRSTn], !!(pin_value & PIN_Trst));
	if ((pin_mask & PIN_Reset) && shield_reset_pin != GPIO_COUNT)
		gpio_set_level(shield_reset_pin, !!(pin_value & PIN_Reset));

	usleep(wait_us);

	tx_buffer[1] = 0;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Set JTAG clock frequency. */
static void dap_swj_clock(size_t peek_c)
{
	uint32_t new_clock_hz, new_half_period_count;

	if (peek_c < 5)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 5);

	memcpy(&new_clock_hz, rx_buffer + 1, sizeof(new_clock_hz));

	if (!new_clock_hz) {
		tx_buffer[1] = STATUS_Error;
	} else {
		new_half_period_count = CPU_CLOCK / new_clock_hz / 2;

		/*
		 * At this point, new_half_period_count contains the number of
		 * CPU clock cycles for a half JTAG clock period.  This will be
		 * used in a wait loop in the bit banging logic.
		 *
		 * Empirically, it has been stablished that at least 50 CPU
		 * clock cycles are used by execution of GPIO manipulations
		 * involved in clock toggling and data shifting, so we subtract
		 * that from the number of cycles that will be "burned" in each
		 * clock phaze while generating the waveform.
		 */
		if (new_half_period_count <= OVERHEAD_CLOCK_CYCLES) {
			/*
			 * Requested speed as at or above the limit, run with no
			 * delay at all.
			 */
			new_half_period_count = 0;
		} else {
			new_half_period_count -= OVERHEAD_CLOCK_CYCLES;
		}

		if (new_half_period_count >= 0x8000) {
			/*
			 * Requested clock is too slow.  Out of range for a
			 * signed 16-bit countdown timer.
			 */
			tx_buffer[1] = STATUS_Error;
		} else {
			jtag_half_period_count = new_half_period_count;
			tx_buffer[1] = STATUS_Ok;
		}
	}
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Busy-wait half a JTAG clock cycle. */
static inline __attribute__((always_inline)) void half_clock_delay(void)
{
	/* Set counter value.  Timer will immediately begin counting down. */
	STM32_TIM_CNT(3) = jtag_half_period_count;
	/*
	 * Wait for counter value to wrap around zero.  Worst case, counting
	 * down from 32767 at a 104Mhz clock frequency will finish in 315us.
	 */
	while (((int16_t)STM32_TIM_CNT(3)) >= 0)
		;
}

/* Clock data out on TMS. */
static void dap_swj_sequence(size_t peek_c)
{
	if (peek_c < 2)
		return;
	unsigned int bit_count = rx_buffer[1] == 0 ? 256 : rx_buffer[1];
	unsigned c = queue_count(&cmsis_dap_rx_queue);
	if (c < 2 + (bit_count + 7) / 8)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, c);
	for (unsigned int i = 0; i < bit_count; i++) {
		gpio_set_level(jtag_pins[JTAG_TMS],
			       !!(rx_buffer[2 + i / 8] & (1 << (i % 8))));
		half_clock_delay();
		gpio_set_level(jtag_pins[JTAG_TCLK], true);
		half_clock_delay();
		gpio_set_level(jtag_pins[JTAG_TCLK], false);
	}
	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/*
 * Do a JTAG transaction, consisting of one or more sequences of clocking data
 * on TDI (between 1 and 64 bits), while keeping TMS at a particular level.
 */
static void dap_jtag_sequence(size_t peek_c)
{
	if (peek_c < 3)
		return;
	int c = queue_count(&cmsis_dap_rx_queue);

	/* Check whether a complete request is in queue. */
	queue_peek_units(&cmsis_dap_rx_queue, rx_buffer, 0, c);
	size_t num_sequences = rx_buffer[1];
	size_t offset = 2;
	for (size_t i = 0; i < num_sequences; i++) {
		uint8_t header = rx_buffer[offset];
		unsigned int bit_count = header & 0x3F;
		if (bit_count == 0)
			bit_count = 0x40;
		offset += 1 + (bit_count + 7) / 8;
		if (offset > c) {
			/* We do not yet have all bytes of the request. */
			return;
		}
	}

	/* We have a complete request, mark as removed from the queue. */
	queue_advance_head(&cmsis_dap_rx_queue, offset);
	/* Prepare output buffer for being populated one bit at a time. */
	memset(tx_buffer + 1, 0, sizeof(tx_buffer) - 1);

	/*
	 * As an optimization, resolve the IO port addresses and masks of
	 * frequently used GPIOs.
	 */
	volatile uint32_t *const jtag_clk_bsrr =
		&STM32_GPIO_BSRR(gpio_list[jtag_pins[JTAG_TCLK]].port);
	const uint32_t jtag_clk_mask_set = gpio_list[jtag_pins[JTAG_TCLK]].mask;
	const uint32_t jtag_clk_mask_clear =
		gpio_list[jtag_pins[JTAG_TCLK]].mask << 16;

	volatile uint32_t *const jtag_tms_bsrr =
		&STM32_GPIO_BSRR(gpio_list[jtag_pins[JTAG_TMS]].port);
	const uint32_t jtag_tms_mask_set = gpio_list[jtag_pins[JTAG_TMS]].mask;
	const uint32_t jtag_tms_mask_clear = gpio_list[jtag_pins[JTAG_TMS]].mask
					     << 16;

	volatile uint32_t *const jtag_tdi_bsrr =
		&STM32_GPIO_BSRR(gpio_list[jtag_pins[JTAG_TDI]].port);
	const uint32_t jtag_tdi_mask_set = gpio_list[jtag_pins[JTAG_TDI]].mask;
	const uint32_t jtag_tdi_mask_clear = gpio_list[jtag_pins[JTAG_TDI]].mask
					     << 16;

	volatile uint16_t *const jtag_tdo_idr =
		&STM32_GPIO_IDR(gpio_list[jtag_pins[JTAG_TDO]].port);
	const uint16_t jtag_tdo_mask = gpio_list[jtag_pins[JTAG_TDO]].mask;

	/* Clock should be low already, but make sure. */
	*jtag_clk_bsrr = jtag_clk_mask_clear;

	/*
	 * Iterate over the list of "sequences", each having a one-byte header
	 * specifying how many bits in the sequence, what the value of TMS
	 * during this sequence, and whether to record TDO during this sequence.
	 */
	const uint8_t *ptr = rx_buffer + 2;
	uint8_t *tx_ptr = tx_buffer + 2;
	const uint8_t *const end = rx_buffer + offset;
	while (ptr < end) {
		/* Consume and decode header byte for this one "sequence". */
		uint8_t header = *ptr++;
		*jtag_tms_bsrr = header & SEQ_Tms ? jtag_tms_mask_set :
						    jtag_tms_mask_clear;
		bool capture_tdo = !!(header & SEQ_CaptureTdo);
		unsigned int bit_count = (((header - 1) & SEQ_NumBits) + 1);

		/*
		 * With TMS set at a given value, clock 1 - 64 bits of data on
		 * TDI/TDO.
		 */
		for (unsigned int i = 0; i < bit_count; i++) {
			*jtag_tdi_bsrr = ptr[i / 8] & (1 << (i % 8)) ?
						 jtag_tdi_mask_set :
						 jtag_tdi_mask_clear;
			half_clock_delay();
			*jtag_clk_bsrr = jtag_clk_mask_set;
			uint32_t tdo_val = !!(*jtag_tdo_idr & jtag_tdo_mask);
			if (capture_tdo) {
				tx_ptr[i / 8] |= tdo_val << (i % 8);
			} else {
				/*
				 * Spend time comparable to memory access above.
				 *
				 * Statement below clears all the bits in the
				 * current output byte which are "ahead" of
				 * where we would be placing the next sampled
				 * bits, that is, into the bit range that was
				 * already zero'ed by memset().
				 */
				tx_ptr[i / 8] &= ~(0xFF << (i % 8));
			}
			half_clock_delay();
			*jtag_clk_bsrr = jtag_clk_mask_clear;
		}
		/* Consume the data bytes of this one "sequence". */
		ptr += (bit_count + 7) / 8;
		if (capture_tdo)
			tx_ptr += (bit_count + 7) / 8;
	}

	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, tx_ptr - tx_buffer);
}

/* Vendor command (HyperDebug): Discover Google-specific capabilities. */
static void dap_goog_info(size_t peek_c)
{
	const uint16_t CAPABILITIES = GOOG_CAP_I2c | GOOG_CAP_I2cDevice |
				      GOOG_CAP_GpioMonitoring |
				      GOOG_CAP_GpioBitbanging;

	if (peek_c < 2)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 2);
	switch (rx_buffer[1]) {
	case GOOG_INFO_Capabilities:
		tx_buffer[1] = sizeof(CAPABILITIES);
		memcpy(tx_buffer + 2, &CAPABILITIES, sizeof(CAPABILITIES));
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer,
				2 + tx_buffer[1]);
		break;
	}
}

/* Map from CMSIS-DAP command byte to handler routine. */
static void (*dispatch_table[256])(size_t peek_c) = {
	[DAP_Info] = dap_info,
	[DAP_GOOG_Info] = dap_goog_info,
	[DAP_GOOG_I2c] = dap_goog_i2c,
	[DAP_GOOG_I2cDevice] = dap_goog_i2c_device,
	[DAP_GOOG_Gpio] = dap_goog_gpio,
	[DAP_HostStatus] = dap_host_status,
	[DAP_Connect] = dap_connect,
	[DAP_Disconnect] = dap_disconnect,
	[DAP_TransferConfigure] = dap_transfer_configure,
	[DAP_ResetTarget] = dap_reset_target,
	[DAP_SWJ_Pins] = dap_swj_pins,
	[DAP_SWJ_Clock] = dap_swj_clock,
	[DAP_SWJ_Sequence] = dap_swj_sequence,
	[DAP_JTAG_Sequence] = dap_jtag_sequence,
};

/* Dispatch incoming request according to table above. */
static void cmsis_dap_dispatch(void)
{
	/* Peek at the incoming data. */
	size_t peek_c = queue_peek_units(&cmsis_dap_rx_queue, rx_buffer, 0, 8);
	if (peek_c < 1) {
		/* Not enough data to start decoding request. */
		return;
	}

	if (dispatch_table[rx_buffer[0]]) {
		/* First byte of response is always same as command byte. */
		tx_buffer[0] = rx_buffer[0];
		/* Invoke handler routine. */
		dispatch_table[rx_buffer[0]](peek_c);
	} else {
		/*
		 * Unrecognized command.  The CMSIS-DAP protocol does not allow
		 * us to know the size of the data of a command in general, nor
		 * is there any command-independent means for sending "not
		 * understood".  The code below discards all queued incoming
		 * data, and sends no reply. */
		queue_advance_head(&cmsis_dap_rx_queue,
				   queue_count(&cmsis_dap_rx_queue));
	}
}

/*
 * Main entry point for handling CMSIS-DAP requests received via USB.
 */
void cmsis_dap_task(void *unused)
{
	while (true) {
		/* Wait for cmsis_dap_written() to wake up this task. */
		task_wait_event(0);
		/* Dispatch CMSIS request, if fully received. */
		cmsis_dap_dispatch();
	}
}

static int command_jtag_set_pins(int argc, const char **argv)
{
	int new_pins[JTAG_INVALID];

	if (argc < 7)
		return EC_ERROR_PARAM_COUNT;

	for (int i = 0; i < JTAG_INVALID; i++) {
		new_pins[i] = gpio_find_by_name(argv[2]);
		if (new_pins[i] == GPIO_COUNT)
			return EC_ERROR_PARAM2 + i;
	}

	/* No errors parsing command line, now apply the new settings. */
	if (jtag_enabled) {
		/*
		 * JTAG left enabled, disable current pins before proceeding.
		 * This will ensure that the next call to dap_connect() will
		 * result in the new set of pins being configured for
		 * input/output as appropriate for JTAG.
		 */
		jtag_enabled = false;
		for (size_t i = 0; i < JTAG_INVALID; i++) {
			gpio_set_flags(jtag_pins[i], saved_pin_flags[i]);
		}
	}

	for (int i = 0; i < JTAG_INVALID; i++)
		jtag_pins[i] = new_pins[i];

	return EC_SUCCESS;
}

static int command_jtag(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "set-pins"))
		return command_jtag_set_pins(argc, argv);
	return 0;
}
DECLARE_CONSOLE_COMMAND_FLAGS(jtag, command_jtag, "",
			      "set-pins <TCLK> <TMS> <TDI> <TDO> <TRSTn>",
			      CMD_FLAG_RESTRICTED);

static void cmsis_dap_reinit(void)
{
	/* Discard any partial requests in the CMSIS-DAP incoming queue. */
	queue_advance_head(&cmsis_dap_rx_queue,
			   queue_count(&cmsis_dap_rx_queue));
	/*
	 * In case JTAG was enabled in dap_connect(), but not properly disabled
	 * with dap_disconnect(), the affected GPIO pins will be restored to
	 * default input setting by hook in `gpio.c`.  In order for next
	 * dap_connect() to have proper effect, below we record the fact that
	 * JTAG connection has been disabled.
	 */
	jtag_enabled = false;
}
DECLARE_HOOK(HOOK_REINIT, cmsis_dap_reinit, HOOK_PRIO_DEFAULT);

/*
 * Declare USB interface for CMSIS-DAP.
 */
USB_STREAM_CONFIG_FULL(cmsis_dap_usb, USB_IFACE_CMSIS_DAP,
		       USB_CLASS_VENDOR_SPEC, USB_SUBCLASS_CMSIS_DAP,
		       USB_PROTOCOL_CMSIS_DAP, USB_STR_CMSIS_DAP_NAME,
		       USB_EP_CMSIS_DAP, USB_MAX_PACKET_SIZE,
		       USB_MAX_PACKET_SIZE, cmsis_dap_rx_queue,
		       cmsis_dap_tx_queue, 0, 1);

static void cmsis_dap_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_CMSIS_DAP);
}

struct consumer_ops const cmsis_dap_consumer_ops = {
	.written = cmsis_dap_written,
};

struct consumer const cmsis_dap_consumer = {
	.queue = &cmsis_dap_rx_queue,
	.ops = &cmsis_dap_consumer_ops,
};

static void cmsis_dap_read(struct producer const *producer, size_t count)
{
	task_wake(TASK_ID_CMSIS_DAP);
}

struct producer_ops const cmsis_dap_producer_ops = {
	.read = cmsis_dap_read,
};

struct producer const cmsis_dap_producer = {
	.queue = &cmsis_dap_tx_queue,
	.ops = &cmsis_dap_producer_ops,
};

struct queue const cmsis_dap_tx_queue = QUEUE_DIRECT(
	sizeof(tx_buffer), uint8_t, cmsis_dap_producer, cmsis_dap_usb.consumer);

struct queue const cmsis_dap_rx_queue = QUEUE_DIRECT(
	sizeof(rx_buffer), uint8_t, cmsis_dap_usb.producer, cmsis_dap_consumer);
