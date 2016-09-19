/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SHI driver for Chrome EC.
 *
 * This uses Input/Output buffer to handle SPI transmission and reception.
 */

#include "chipset.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SPI, format, ## args)

#if !(DEBUG_SHI)
#define DEBUG_CPUTS(...)
#define DEBUG_CPRINTS(...)
#define DEBUG_CPRINTF(...)
#else
#define DEBUG_CPUTS(outstr) cputs(CC_SPI, outstr)
#define DEBUG_CPRINTS(format, args...) cprints(CC_SPI, format, ## args)
#define DEBUG_CPRINTF(format, args...) cprintf(CC_SPI, format, ## args)
#endif

/* SHI Bus definition */
#define SHI_OBUF_FULL_SIZE  64                     /* Full output buffer size */
#define SHI_IBUF_FULL_SIZE  64                     /* Full input buffer size  */
#define SHI_OBUF_HALF_SIZE  (SHI_OBUF_FULL_SIZE/2) /* Half output buffer size */
#define SHI_IBUF_HALF_SIZE  (SHI_IBUF_FULL_SIZE/2) /* Half input buffer size  */

/* Start address of SHI output buffer */
#define SHI_OBUF_START_ADDR  (volatile uint8_t  *)(NPCX_SHI_BASE_ADDR + 0x020)
/* Middle address of SHI output buffer */
#define SHI_OBUF_HALF_ADDR   (SHI_OBUF_START_ADDR + SHI_OBUF_HALF_SIZE)
/* Top address of SHI output buffer */
#define SHI_OBUF_FULL_ADDR   (SHI_OBUF_START_ADDR + SHI_IBUF_FULL_SIZE)
/*
 * Valid offset of SHI output buffer to write.
 * When SIMUL bit is set, IBUFPTR can be used instead of OBUFPTR
 */
#define SHI_OBUF_VALID_OFFSET ((shi_read_buf_pointer() + \
			SHI_OUT_PREAMBLE_LENGTH) % SHI_OBUF_FULL_SIZE)
/* Start address of SHI input buffer */
#define SHI_IBUF_START_ADDR  (volatile uint8_t  *)(NPCX_SHI_BASE_ADDR + 0x060)
/* Current address of SHI input buffer */
#define SHI_IBUF_CUR_ADDR    (SHI_IBUF_START_ADDR + shi_read_buf_pointer())

/*
 * Timeout to wait for SHI request packet
 *
 * This affects the slowest SPI clock we can support.  A delay of 8192 us
 * permits a 512-byte request at 500 KHz, assuming the master starts sending
 * bytes as soon as it asserts chip select.  That's as slow as we would
 * practically want to run the SHI interface, since running it slower
 * significantly impacts firmware update times.
 */
#define SHI_CMD_RX_TIMEOUT_US 8192

/* Timeout for glitch case. Make sure it will exceed 8 SPI clocks */
#define SHI_GLITCH_TIMEOUT_US 10000

/*
 * The AP blindly clocks back bytes over the SPI interface looking for a
 * framing byte.  So this preamble must always precede the actual response
 * packet.
 */

#define SHI_OUT_PREAMBLE_LENGTH 2
/*
 * Space allocation of the past-end status byte (EC_SPI_PAST_END) in the out_msg
 * buffer.
 */
#define EC_SPI_PAST_END_LENGTH 1
/*
 * Space allocation of the frame status byte (EC_SPI_FRAME_START) in the out_msg
 * buffer.
 */
#define EC_SPI_FRAME_START_LENGTH 1

/*
 * Offset of output parameters needs to account for pad and framing bytes and
 * one last past-end byte at the end so any additional bytes clocked out by
 * the AP will have a known and identifiable value.
 */
#define SHI_PROTO3_OVERHEAD (EC_SPI_PAST_END_LENGTH + EC_SPI_FRAME_START_LENGTH)


#ifdef NPCX_SHI_BYPASS_OVER_256B
/* The boundary which SHI will output invalid data on MISO. */
#define SHI_BYPASS_BOUNDARY 256
/* Increase FRAME_START_LENGTH in case shi outputs invalid FRAME_START byte */
#undef  EC_SPI_FRAME_START_LENGTH
#define EC_SPI_FRAME_START_LENGTH 2
#endif

/*
 * Max data size for a version 3 request/response packet.  This is big enough
 * to handle a request/response header, flash write offset/size, and 512 bytes
 * of flash data.
 */
#define SHI_MAX_REQUEST_SIZE 0x220

#ifdef NPCX_SHI_BYPASS_OVER_256B
/* Make sure SHI_MAX_RESPONSE_SIZE won't exceed 256 bytes */
#define SHI_MAX_RESPONSE_SIZE (160 + EC_SPI_PAST_END_LENGTH + \
		EC_SPI_FRAME_START_LENGTH + sizeof(struct ec_host_response))
BUILD_ASSERT(SHI_MAX_RESPONSE_SIZE <= SHI_BYPASS_BOUNDARY);
#else
#define SHI_MAX_RESPONSE_SIZE 0x220
#endif

/*
 * Our input and output msg buffers. These must be large enough for our largest
 * message, including protocol overhead, and must be 32-bit aligned.
 */
static uint8_t out_msg[SHI_MAX_RESPONSE_SIZE];
static uint8_t in_msg[SHI_MAX_REQUEST_SIZE];

/* Parameters used by host protocols */
static struct host_packet shi_packet;

enum shi_state {
	/* SHI not enabled (initial state, and when chipset is off) */
	SHI_STATE_DISABLED = 0,
	/* Ready to receive next request */
	SHI_STATE_READY_TO_RECV,
	/* Receiving request */
	SHI_STATE_RECEIVING,
	/* Processing request */
	SHI_STATE_PROCESSING,
	/* Canceling response since CS deasserted and output NOT_READY byte */
	SHI_STATE_CNL_RESP_NOT_RDY,
#ifdef NPCX_SHI_BYPASS_OVER_256B
	/* Keep output buffer as PROCESSING byte until reaching 256B boundary */
	SHI_STATE_WAIT_ALIGNMENT,
#endif
	/* Sending response */
	SHI_STATE_SENDING,
	/* Received data is valid. */
	SHI_STATE_BAD_RECEIVED_DATA,
};

volatile enum shi_state state;

/* SHI bus parameters */
struct shi_bus_parameters {
	uint8_t *rx_msg;          /* Entry pointer of msg rx buffer   */
	uint8_t *tx_msg;          /* Entry pointer of msg tx buffer   */
	volatile uint8_t *rx_buf; /* Entry pointer of receive buffer  */
	volatile uint8_t *tx_buf; /* Entry pointer of transmit buffer */
	uint16_t sz_received;     /* Size of received data in bytes   */
	uint16_t sz_sending;      /* Size of sending data in bytes    */
	uint16_t sz_request;      /* request bytes need to receive    */
	uint16_t sz_response;     /* response bytes need to receive   */
	timestamp_t rx_deadline;  /* deadline of receiving            */
	uint8_t  pre_ibufstat;    /* Previous IBUFSTAT value          */
#ifdef NPCX_SHI_BYPASS_OVER_256B
	uint16_t bytes_in_256b;   /* Sent bytes in 256 bytes boundary */
#endif
} shi_params;

/* Forward declaraction */
static void shi_reset_prepare(void);
static void shi_bad_received_data(void);
static void shi_fill_out_status(uint8_t status);
static void shi_write_half_outbuf(void);
static void shi_write_first_pkg_outbuf(uint16_t szbytes);
static int shi_read_inbuf_wait(uint16_t szbytes);
static uint8_t shi_read_buf_pointer(void);

/*****************************************************************************/
/* V3 protocol layer functions */

/**
 * Called to send a response back to the host.
 *
 * Some commands can continue for a while. This function is called by
 * host_command when it completes.
 *
 */
static void shi_send_response_packet(struct host_packet *pkt)
{
	/*
	 * Disable interrupts. This routine is not called from interrupt
	 * context and buffer underrun will likely occur if it is
	 * preempted after writing its initial reply byte. Also, we must be
	 * sure our state doesn't unexpectedly change, in case we're expected
	 * to take RESP_NOT_RDY actions.
	 */
	interrupt_disable();
	if (state == SHI_STATE_PROCESSING) {
		/* Append our past-end byte, which we reserved space for. */
		((uint8_t *) pkt->response)[pkt->response_size + 0] =
			EC_SPI_PAST_END;

		/* Computing sending bytes of response */
		shi_params.sz_response =
			pkt->response_size + SHI_PROTO3_OVERHEAD;

		/* Start to fill output buffer with msg buffer */
		shi_write_first_pkg_outbuf(shi_params.sz_response);
#ifdef NPCX_SHI_BYPASS_OVER_256B
		/*
		 * If response package is over 256B boundary,
		 * keep sending PROCESSING byte
		 */
		if (state != SHI_STATE_WAIT_ALIGNMENT) {
#endif
			/* Transmit the reply */
			state = SHI_STATE_SENDING;
			DEBUG_CPRINTF("SND-");
#ifdef NPCX_SHI_BYPASS_OVER_256B
		}
#endif
	}
	/*
	 * If we're not processing, then the AP has already terminated the
	 * transaction, and won't be listening for a response.
	 * Reset state machine for next transaction.
	 */
	else if (state == SHI_STATE_CNL_RESP_NOT_RDY) {
		shi_reset_prepare();
		DEBUG_CPRINTF("END\n");
	} else
		DEBUG_CPRINTS("Unexpected state %d in response handler", state);
	interrupt_enable();
}

void shi_handle_host_package(void)
{
	uint16_t sz_inbuf_int = shi_params.sz_request / SHI_IBUF_HALF_SIZE;
	uint16_t cnt_inbuf_int = shi_params.sz_received / SHI_IBUF_HALF_SIZE;
	if (sz_inbuf_int - cnt_inbuf_int)
		/* Need to receive data from buffer */
		return;
	else {
		uint16_t remain_bytes = shi_params.sz_request
					- shi_params.sz_received;

		/* Read remaining bytes from input buffer directly */
		if (!shi_read_inbuf_wait(remain_bytes))
			return shi_bad_received_data();
		/* Move to processing state immediately */
		state = SHI_STATE_PROCESSING;
		DEBUG_CPRINTF("PRC-");
	}
	/* Fill output buffer to indicate we`re processing request */
	shi_fill_out_status(EC_SPI_PROCESSING);

	/* Set up parameters for host request */
	shi_packet.send_response = shi_send_response_packet;

	shi_packet.request = in_msg;
	shi_packet.request_temp = NULL;
	shi_packet.request_max = sizeof(in_msg);
	shi_packet.request_size = shi_params.sz_request;


#ifdef NPCX_SHI_BYPASS_OVER_256B
	/* Move FRAME_START to second byte */
	out_msg[0] = EC_SPI_PROCESSING;
	out_msg[1] = EC_SPI_FRAME_START;
#else
	/* Put FRAME_START in first byte */
	out_msg[0] = EC_SPI_FRAME_START;
#endif
	shi_packet.response = out_msg + EC_SPI_FRAME_START_LENGTH;

	/* Reserve space for frame start and trailing past-end byte */
	shi_packet.response_max = sizeof(out_msg) - SHI_PROTO3_OVERHEAD;
	shi_packet.response_size = 0;
	shi_packet.driver_result = EC_RES_SUCCESS;

	/* Go to common-layer to handle request */
	host_packet_receive(&shi_packet);
}

/* Parse header for version of spi-protocol */
static void shi_parse_header(void)
{
	/* We're now inside a transaction */
	state = SHI_STATE_RECEIVING;
	DEBUG_CPRINTF("RV-");

	/* Setup deadline time for receiving */
	shi_params.rx_deadline = get_time();
	shi_params.rx_deadline.val += SHI_CMD_RX_TIMEOUT_US;

	/* Wait for version, command, length bytes */
	if (!shi_read_inbuf_wait(3))
		return shi_bad_received_data();

	if (in_msg[0] == EC_HOST_REQUEST_VERSION) {
		/* Protocol version 3 */
		struct ec_host_request *r = (struct ec_host_request *) in_msg;
		int pkt_size;
		/*
		 * If request is over 32 bytes,
		 * we need to modified the algorithm again.
		 */
		ASSERT(sizeof(*r) < SHI_IBUF_HALF_SIZE);

		/* Wait for the rest of the command header */
		if (!shi_read_inbuf_wait(sizeof(*r) - 3))
			return shi_bad_received_data();

		/* Check how big the packet should be */
		pkt_size = host_request_expected_size(r);
		if (pkt_size == 0 || pkt_size > sizeof(in_msg))
			return shi_bad_received_data();

		/* Computing total bytes need to receive */
		shi_params.sz_request = pkt_size;

		shi_handle_host_package();
	} else {
		/* Invalid version number */
		return shi_bad_received_data();
	}
}

/*****************************************************************************/
/* IC specific low-level driver */

/* This routine fills out all SHI output buffer with status byte */
static void shi_fill_out_status(uint8_t status)
{
	uint16_t i;
	uint16_t offset = SHI_OBUF_VALID_OFFSET;

	/* Disable interrupts in case the interfere by the other interrupts */
	interrupt_disable();

	/* Fill out all output buffer with status byte */
	for (i = offset; i < SHI_OBUF_FULL_SIZE; i++)
		NPCX_OBUF(i) = status;
	for (i = 0; i < offset; i++)
		NPCX_OBUF(i) = status;

	/* End of critical section */
	interrupt_enable();
}

/*
 * This routine makes sure it's valid transaction or glitch on CS bus.
 */
static int shi_is_cs_glitch(void)
{
	timestamp_t deadline;

	deadline.val = get_time().val + SHI_GLITCH_TIMEOUT_US;
	/*
	 * If input buffer pointer is no changed after timeout, it will
	 * return true
	 */
	while (shi_params.pre_ibufstat == shi_read_buf_pointer())
		if (timestamp_expired(deadline, NULL))
			return 1;
	/* valid package */
	return 0;
}

/*
 * This routine write SHI next half output buffer from msg buffer
 */
static void shi_write_half_outbuf(void)
{
	uint16_t i;
	uint16_t size = MIN(SHI_OBUF_HALF_SIZE,
			shi_params.sz_response - shi_params.sz_sending);
	/* Fill half output buffer */
	for (i = 0; i < size; i++, shi_params.sz_sending++)
		*(shi_params.tx_buf++) = *(shi_params.tx_msg++);
}

/*
 * This routine write SHI output buffer from msg buffer over halt of it.
 * It make sure we have enought time to handle next operations.
 */
static void shi_write_first_pkg_outbuf(uint16_t szbytes)
{
	uint16_t i;
	uint16_t offset, size;

#ifdef NPCX_SHI_BYPASS_OVER_256B
	/*
	 * If response package is across 256 bytes boundary,
	 * bypass needs to extend PROCESSING bytes after reaching the boundary.
	 */
	if (shi_params.bytes_in_256b + SHI_OBUF_FULL_SIZE + szbytes
	    > SHI_BYPASS_BOUNDARY) {
		state = SHI_STATE_WAIT_ALIGNMENT;
		/* Set pointer of output buffer to the start address */
		shi_params.tx_buf = SHI_OBUF_START_ADDR;
		DEBUG_CPRINTF("WAT-");
		return;
	}
#endif

	offset = SHI_OBUF_VALID_OFFSET;
	shi_params.tx_buf = SHI_OBUF_START_ADDR + offset;
	/* Fill half output buffer */
	size = MIN(SHI_OBUF_HALF_SIZE - (offset % SHI_OBUF_HALF_SIZE),
					szbytes - shi_params.sz_sending);
	for (i = 0; i < size; i++, shi_params.sz_sending++)
		*(shi_params.tx_buf++) = *(shi_params.tx_msg++);

	/* Write data from bottom address again */
	if (shi_params.tx_buf == SHI_OBUF_FULL_ADDR)
		shi_params.tx_buf = SHI_OBUF_START_ADDR;

	/* Fill next half output buffer */
	size = MIN(SHI_OBUF_HALF_SIZE, szbytes - shi_params.sz_sending);
	for (i = 0; i < size; i++, shi_params.sz_sending++)
		*(shi_params.tx_buf++) = *(shi_params.tx_msg++);
}

/* This routine copies SHI half input buffer data to msg buffer */
static void shi_read_half_inbuf(void)
{
	/*
	 * Copy to read buffer until reaching middle/top address of
	 * input buffer or completing receiving data
	 */
	do {
		/* Restore data to msg buffer */
		*(shi_params.rx_msg++) = *(shi_params.rx_buf++);
		shi_params.sz_received++;
	} while (shi_params.sz_received % SHI_IBUF_HALF_SIZE
		&& shi_params.sz_received != shi_params.sz_request);
}

/*
 * This routine read SHI input buffer to msg buffer until
 * we have received a certain number of bytes
 */
static int shi_read_inbuf_wait(uint16_t szbytes)
{
	uint16_t i;

	/* Copy data to msg buffer from input buffer */
	for (i = 0; i < szbytes; i++, shi_params.sz_received++) {
		/*
		 * If input buffer pointer equals pointer which wants to read,
		 * it means data is not ready.
		 */
		while (shi_params.rx_buf == SHI_IBUF_CUR_ADDR)
			if (timestamp_expired(shi_params.rx_deadline, NULL))
				return 0;
		/* Restore data to msg buffer */
		*(shi_params.rx_msg++) = *(shi_params.rx_buf++);
	}
	return 1;
}

/* Read pointer of input or output buffer by consecutive reading */
static uint8_t shi_read_buf_pointer(void)
{
	uint8_t stat;
	/* Wait for two consecutive equal values are read */
	do {
		stat = NPCX_IBUFSTAT;
	} while (stat != NPCX_IBUFSTAT);

	return stat;
}

/* This routine handles shi recevied unexcepted data */
static void shi_bad_received_data(void)
{
	uint16_t i;

	/* State machine mismatch, timeout, or protocol we can't handle. */
	shi_fill_out_status(EC_SPI_RX_BAD_DATA);
	state = SHI_STATE_BAD_RECEIVED_DATA;

	CPRINTF("BAD-");
	CPRINTF("in_msg=[");
	for (i = 0; i < shi_params.sz_received; i++)
		CPRINTF("%02x ", in_msg[i]);
	CPRINTF("]\n");

	/* Reset shi's state machine for error recovery */
	shi_reset_prepare();

	DEBUG_CPRINTF("END\n");
}

/*
 * Avoid spamming the console with prints every IBF / IBHF interrupt, if
 * we find ourselves in an unexpected state.
 */
static int last_error_state = -1;

static void log_unexpected_state(char *isr_name)
{
#if !(DEBUG_SHI)
	if (state != last_error_state)
		CPRINTS("Unexpected state %d in %s ISR", state, isr_name);
#endif
	last_error_state = state;
}

/* This routine handles all interrupts of this module */
void shi_int_handler(void)
{
	uint8_t stat_reg;

	/* Read status register and clear interrupt status early*/
	stat_reg = NPCX_EVSTAT;
	NPCX_EVSTAT = stat_reg;

	/*
	 * End of data for read/write transaction. ie SHI_CS is deasserted.
	 * Host completed or aborted transaction
	 */
	if (IS_BIT_SET(stat_reg, NPCX_EVSTAT_EOR)) {
		/*
		 * We're not in proper state.
		 * Mark not ready to abort next transaction
		 */
		DEBUG_CPRINTF("CSH-");
		/*
		 * If the buffer is still used by the host command.
		 * Change state machine for response handler.
		 */
		if (state == SHI_STATE_PROCESSING) {
			/*
			 * Mark not ready to prevent the other
			 * transaction immediately
			 */
			shi_fill_out_status(EC_SPI_NOT_READY);

			state = SHI_STATE_CNL_RESP_NOT_RDY;

			/*
			 * Disable SHI interrupt, it will remain disabled
			 * until shi_send_response_packet() is called and
			 * CS is asserted for a new transaction.
			 */
			task_disable_irq(NPCX_IRQ_SHI);

			DEBUG_CPRINTF("CNL-");
			return;
		/* Next transaction but we're not ready */
		} else if (state == SHI_STATE_CNL_RESP_NOT_RDY)
			return;

		/* Error state for checking*/
		if (state != SHI_STATE_SENDING)
			log_unexpected_state("IBEOR");

		/* reset SHI and prepare to next transaction again */
		shi_reset_prepare();
		DEBUG_CPRINTF("END\n");
		return;
	}

	/*
	 * Indicate input/output buffer pointer reaches the half buffer size.
	 * Transaction is processing.
	 */
	if (IS_BIT_SET(stat_reg, NPCX_EVSTAT_IBHF)) {
		if (state == SHI_STATE_RECEIVING) {
			/* Read data from input to msg buffer */
			shi_read_half_inbuf();
			return shi_handle_host_package();
		} else if (state == SHI_STATE_SENDING) {
			/* Write data from msg buffer to output buffer */
			if (shi_params.tx_buf == SHI_OBUF_START_ADDR +
					SHI_OBUF_FULL_SIZE) {
				/* Write data from bottom address again */
				shi_params.tx_buf = SHI_OBUF_START_ADDR;
				return shi_write_half_outbuf();
			} else /* ignore it */
				return;
		} else if (state == SHI_STATE_PROCESSING) {
			/* Wait for host to handle request */
		}
#ifdef NPCX_SHI_BYPASS_OVER_256B
		else if (state == SHI_STATE_WAIT_ALIGNMENT) {
			/*
			 * If pointer of output buffer will reach 256 bytes
			 * boundary soon, start to fill response data.
			 */
			if (shi_params.bytes_in_256b == SHI_BYPASS_BOUNDARY -
					SHI_OBUF_FULL_SIZE) {
				state = SHI_STATE_SENDING;
				DEBUG_CPRINTF("SND-");
				return shi_write_half_outbuf();
			}
		}
#endif
		else
			/* Unexpected status */
			log_unexpected_state("IBHF");
	}

	/*
	 * Indicate input/output buffer pointer reaches the full buffer size.
	 * Transaction is processing.
	 */
	if (IS_BIT_SET(stat_reg, NPCX_EVSTAT_IBF)) {
#ifdef NPCX_SHI_BYPASS_OVER_256B
		/* Record the sent bytes within 256B boundary */
		shi_params.bytes_in_256b = (shi_params.bytes_in_256b +
				SHI_OBUF_FULL_SIZE) % SHI_BYPASS_BOUNDARY;
#endif
		if (state == SHI_STATE_RECEIVING) {
			/* read data from input to msg buffer */
			shi_read_half_inbuf();
			/* Read to bottom address again */
			shi_params.rx_buf = SHI_IBUF_START_ADDR;
			return shi_handle_host_package();
		} else if (state == SHI_STATE_SENDING)
			/* Write data from msg buffer to output buffer */
			if (shi_params.tx_buf == SHI_OBUF_START_ADDR +
					SHI_OBUF_HALF_SIZE)
				return shi_write_half_outbuf();
			else /* ignore it */
				return;
		else if (state == SHI_STATE_PROCESSING
#ifdef NPCX_SHI_BYPASS_OVER_256B
				|| state == SHI_STATE_WAIT_ALIGNMENT
#endif
				)
			/* Wait for host handles request */
			return;
		else
			/* Unexpected status */
			log_unexpected_state("IBF");
	}
}
DECLARE_IRQ(NPCX_IRQ_SHI, shi_int_handler, 1);

/* Handle an CS assert event on the SHI_CS_L pin */
void shi_cs_event(enum gpio_signal signal)
{
	/* If not enabled, ignore glitches on SHI_CS_L */
	if (state == SHI_STATE_DISABLED)
		return;

	/*
	 * IBUFSTAT resets on the 7th clock cycle after CS assertion, which
	 * may not have happened yet. We use NPCX_IBUFSTAT for calculating
	 * buffer fill depth, so make sure it's valid before proceeding.
	 */
	if (shi_is_cs_glitch()) {
		CPRINTS("ERR-GTH");
		shi_reset_prepare();
		DEBUG_CPRINTF("END\n");
		return;
	}

	/* NOT_READY should be sent and there're no spi transaction now. */
	if (state == SHI_STATE_CNL_RESP_NOT_RDY)
		return;

	/* Chip select is low = asserted */
	if (state != SHI_STATE_READY_TO_RECV) {
		/* State machine should be reset in EVSTAT_EOR ISR */
		CPRINTS("Unexpected state %d in CS ISR", state);
		return;
	}

	DEBUG_CPRINTF("CSL-");

	/*
	 * Clear possible EOR event from previous transaction since it's
	 * irrelevant now that CS is re-asserted.
	 */
	SET_BIT(NPCX_EVSTAT, NPCX_EVSTAT_EOR);

	/*
	 * Enable SHI interrupt - we will either succeed to parse our host
	 * command or reset on failure from here.
	 */
	task_enable_irq(NPCX_IRQ_SHI);

	/* Read first three bytes to parse which protocol is receiving */
	shi_parse_header();
}

/*****************************************************************************/
/* Hook functions for chipset and initialization */

/*
 * Reset SHI bus and prepare next transaction
 * Please make sure it is executed when there're no spi transactions
 */
static void shi_reset_prepare(void)
{
	uint16_t i;

	/* We no longer care about SHI interrupts, so disable them. */
	task_disable_irq(NPCX_IRQ_SHI);

	/* Disable SHI unit to clear all status bits */
	CLEAR_BIT(NPCX_SHICFG1, NPCX_SHICFG1_EN);

	/* Initialize parameters of next transaction */
	shi_params.rx_msg = in_msg;
	shi_params.tx_msg = out_msg;
	shi_params.rx_buf = SHI_IBUF_START_ADDR;
	shi_params.tx_buf = SHI_OBUF_HALF_ADDR;
	shi_params.sz_received = 0;
	shi_params.sz_sending = 0;
	shi_params.sz_request = 0;
	shi_params.sz_response = 0;
#ifdef NPCX_SHI_BYPASS_OVER_256B
	shi_params.bytes_in_256b = 0;
#endif
	/* Record last IBUFSTAT for glitch case */
	shi_params.pre_ibufstat = shi_read_buf_pointer();

	/*
	 * Fill output buffer to indicate we`re
	 * ready to receive next transaction.
	 */
	for (i = 1; i < SHI_OBUF_FULL_SIZE; i++)
		NPCX_OBUF(i) = EC_SPI_RECEIVING;
	NPCX_OBUF(0) = EC_SPI_OLD_READY;

	/* Enable SHI & WEN functionality */
	NPCX_SHICFG1 = 0x85;

	/* Ready to receive */
	state = SHI_STATE_READY_TO_RECV;
	last_error_state = -1;

	DEBUG_CPRINTF("RDY-");
}

static void shi_enable(void)
{
	int gpio_flags;

	shi_reset_prepare();

	/* Ensure SHI_CS_L interrupt is disabled */
	gpio_disable_interrupt(GPIO_SHI_CS_L);

	/* Enable PU, if requested */
	gpio_flags = GPIO_INPUT | GPIO_INT_F_FALLING;
#ifdef NPCX_SHI_CS_PU
	gpio_flags |= GPIO_PULL_UP;
#endif
	gpio_set_flags(GPIO_SHI_CS_L, gpio_flags);

	/*
	 * Mux SHI related pins
	 * SHI_SDI SHI_SDO SHI_CS# SHI_SCLK are selected to device pins
	 */
	SET_BIT(NPCX_DEVALT(ALT_GROUP_C), NPCX_DEVALTC_SHI_SL);

	task_clear_pending_irq(NPCX_IRQ_SHI);

	/* Enable SHI_CS_L interrupt */
	gpio_enable_interrupt(GPIO_SHI_CS_L);

	/*
	 * If CS is already asserted prior to enabling our GPIO interrupt then
	 * we have missed the falling edge and we need to handle the
	 * deassertion interrupt.
	 */
	task_enable_irq(NPCX_IRQ_SHI);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, shi_enable, HOOK_PRIO_DEFAULT);

static void shi_reenable_on_sysjump(void)
{
#if !(DEBUG_SHI)
	if (system_jumped_to_this_image() && chipset_in_state(CHIPSET_STATE_ON))
#endif
		shi_enable();
}
/* Call hook after chipset sets initial power state */
DECLARE_HOOK(HOOK_INIT,
	     shi_reenable_on_sysjump,
	     HOOK_PRIO_INIT_CHIPSET + 1);

/* Disable SHI bus */
static void shi_disable(void)
{
	state = SHI_STATE_DISABLED;

	task_disable_irq(NPCX_IRQ_SHI);

	/* Disable SHI_CS_L interrupt */
	gpio_disable_interrupt(GPIO_SHI_CS_L);

	/* Restore SHI_CS_L back to default state */
	gpio_reset(GPIO_SHI_CS_L);

	/*
	 * Mux SHI related pins
	 * SHI_SDI SHI_SDO SHI_CS# SHI_SCLK are selected to GPIO
	 */
	CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_C), NPCX_DEVALTC_SHI_SL);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, shi_disable, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_SYSJUMP, shi_disable, HOOK_PRIO_DEFAULT);

static void shi_init(void)
{
	/* Power on SHI module first */
	CLEAR_BIT(NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_5), NPCX_PWDWN_CTL5_SHI_PD);

	/*
	 * SHICFG1 (SHI Configuration 1) setting
	 * [7] - IWRAP	= 1: Wrap input buffer to the first address
	 * [6] - CPOL	= 0: Sampling on rising edge and output on falling edge
	 * [5] - DAS	= 0: return STATUS reg data after Status command
	 * [4] - AUTOBE	= 0: Automatically update the OBES bit in STATUS reg
	 * [3] - AUTIBF	= 0: Automatically update the IBFS bit in STATUS reg
	 * [2] - WEN    = 0: Enable host write to input buffer
	 * [1] - Reserved 0
	 * [0] - ENABLE	= 0: Disable SHI at the beginning
	 */
	NPCX_SHICFG1 = 0x80;

	/*
	 * SHICFG2 (SHI Configuration 2) setting
	 * [7] - Reserved 0
	 * [6] - REEVEN = 0: Restart events are not used
	 * [5] - Reserved 0
	 * [4] - REEN   = 0: Restart transactions are not used
	 * [3] - SLWU   = 0: Seem-less wake-up is enabled by default
	 * [2] - ONESHOT= 0: WEN is cleared at the end of a write transaction
	 * [1] - BUSY   = 0: SHI bus is busy 0: idle.
	 * [0] - SIMUL	= 1: Turn on simultaneous Read/Write
	 */
	NPCX_SHICFG2 = 0x01;

	/*
	 * EVENABLE (Event Enable) setting
	 * [7] - IBOREN = 0: Input buffer overrun interrupt enable
	 * [6] - STSREN = 0: status read interrupt disable
	 * [5] - EOWEN  = 0: End-of-Data for Write Transaction Interrupt Enable
	 * [4] - EOREN  = 1: End-of-Data for Read Transaction Interrupt Enable
	 * [3] - IBHFEN = 1: Input Buffer Half Full Interrupt Enable
	 * [2] - IBFEN  = 1: Input Buffer Full Interrupt Enable
	 * [1] - OBHEEN = 0: Output Buffer Half Empty Interrupt Enable
	 * [0] - OBEEN  = 0: Output Buffer Empty Interrupt Enable
	 */
	NPCX_EVENABLE = 0x1C;

	/* Clear SHI events status register */
	NPCX_EVSTAT = 0XFF;
}
/* Call hook before chipset sets initial power state and calls resume hooks */
DECLARE_HOOK(HOOK_INIT, shi_init, HOOK_PRIO_INIT_CHIPSET - 1);

/**
 * Get protocol information
 */
static int shi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = (1 << 3);
	r->max_request_packet_size = SHI_MAX_REQUEST_SIZE;
	r->max_response_packet_size = SHI_MAX_RESPONSE_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, shi_get_protocol_info,
EC_VER_MASK(0));
