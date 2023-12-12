/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART Host Command Interface for Chrome EC */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "uartn.h"
#include "uartn_dma.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

/*
 * The UART host command interface relies on MDMA module which is supported
 * in npcx9 and later chips.
 */
#if NPCX_FAMILY_VERSION < NPCX_FAMILY_NPCX9
#error "Chip family cannot support UART host command"
#endif

BUILD_ASSERT(CONFIG_UART_HOST_COMMAND_HW < NPCX_UART_COUNT);

/*
 * Timeout to wait for complete request packet
 *
 * This value determines how long we should wait for entire packet to arrive.
 * UART host command handler should wait for at least 75% of
 * EC_MSG_DEADLINE_MS, before declaring timeout and dropping the packet.
 *
 * This timeout should be less than host's driver timeout to make sure that
 * last packet can be successfully discarded before AP attempts to resend
 * request. AP driver waits for EC_MSG_DEADLINE_MS = 200 before attempting a
 * retry.
 */
#define UART_REQ_RX_TIMEOUT (150 * MSEC)

/*
 * Timeout to wait for overrun bytes on UART
 *
 * This values determines how long call to process_request should be deferred
 * in case host is sending extra bytes. This value is based on DMA buffer size.
 *
 * There is no guarantee that AP will send continuous bytes on usart. Wait
 * for UART_DEFERRED_PROCESS_REQ_TIMEOUT_US to check if host is sending
 * extra bytes.
 * Note: This value affects the response latency.
 */
#define UART_DEFERRED_PROCESS_REQ_TIMEOUT 300

#define UART_HOST_CMD_HW CONFIG_UART_HOST_COMMAND_HW
#define UART_HOST_CMD_MAX_REQ_SIZE 0x220
#define UART_HOST_CMD_MAX_RSP_SIZE 0x100

#if (UART_HOST_CMD_HW == 0)
#define UART_HOST_COMMAND_IRQ NPCX_IRQ_UART
#elif (UART_HOST_CMD_HW == 1)
#define UART_HOST_COMMAND_IRQ NPCX_IRQ_UART2
#elif (UART_HOST_CMD_HW == 2)
#define UART_HOST_COMMAND_IRQ NPCX_IRQ_UART3
#elif (UART_HOST_CMD_HW == 3)
#define UART_HOST_COMMAND_IRQ NPCX_IRQ_UART4
#endif

static uint8_t uart_host_cmd_in_buf[UART_HOST_CMD_MAX_REQ_SIZE] __aligned(4);
static uint8_t uart_host_cmd_out_buf[UART_HOST_CMD_MAX_RSP_SIZE] __aligned(4);

/*
 * Maintain head position of in buffer
 * Head always starts with zero and goes up to max bytes.
 * Once the buffer contents are read, it should go back to zero.
 */
static uint16_t uart_dma_in_head;
static uint16_t uart_dma_in_head_old;

/*
 * Maintain head position of out buffer
 * Head always starts from zero and goes up to max bytes.
 * Head is moved by tx interrupt handler to response size sent by host command
 * task. Once all the bytes are sent (head == tail) both should go back to 0.
 */
static uint16_t uart_out_head;

/*
 * Once the response is ready, get the datalen
 */
static uint16_t uart_out_datalen;

/*
 * Enumeration to maintain different states of incoming request from
 * host
 */
static enum uart_host_command_state {
	/*
	 * UART host command handler not enabled.
	 */
	UART_HOST_CMD_STATE_DISABLED,

	/*
	 * Ready to receive next request
	 * This state represents USART layer is initialized and ready to
	 * receive host request. Once the response is sent, current_state is
	 * reset to this state to accept next packet.
	 */
	UART_HOST_CMD_READY_TO_RX,

	/*
	 * Receiving request
	 * After first byte is received current_state is moved to receiving
	 * state until all the header bytes + datalen bytes are received.
	 * If host_request_timeout was called in this state, it would be
	 * because of an underrun situation.
	 */
	UART_HOST_CMD_RECEIVING,

	/*
	 * Receiving complete
	 * Once all the header bytes + datalen bytes are received, current_state
	 * is moved to complete. Ideally, host should wait for response or retry
	 * timeout before sending anymore bytes, otherwise current_state will
	 * be moved to overrun to represent extra bytes sent by host.
	 */
	UART_HOST_CMD_COMPLETE,

	/*
	 * Processing request
	 * Once the process_request starts processing usart_in_buffer,
	 * current_state is moved to processing state. Host should not send
	 * any bytes in this state as it would be considered contiguous
	 * request.
	 */
	UART_HOST_CMD_PROCESSING,

	/*
	 * Sending response
	 * Once host task is ready with the response bytes, current_state is
	 * moved to sending state.
	 */
	UART_HOST_CMD_SENDING,

	/*
	 * Received bad data
	 * If bad packet header is received, current_state is moved to rx_bad
	 * state and after rx_timeout all the bytes are dropped.
	 */
	UART_HOST_CMD_RX_BAD,

	/*
	 * Receiving data overrun bytes
	 * If extra bytes are received after current_state is in complete,
	 * host is sending extra bytes which indicates data overrun.
	 */
	UART_HOST_CMD_RX_OVERRUN,

} current_state __aligned(4);

static void usart_host_command_request_timeout(void);
static void usart_host_command_process_request(void);
static void uart_host_command_process_response(struct host_packet *pkt);
void uart_host_command_reset(void);

/*
 * This function will be called only if request rx timed out.
 * Drop the packet and put tl state into RX_READY
 */
static void usart_host_command_request_timeout(void)
{
	switch (current_state) {
	case UART_HOST_CMD_RECEIVING:
		/* If state is receiving then timeout was hit due to underrun */
		CPRINTS("USART HOST CMD ERROR: Request underrun detected.");
		break;

	case UART_HOST_CMD_RX_OVERRUN:
		/* If state is rx_overrun then timeout was hit because
		 * process request was cancelled and extra rx bytes were
		 * dropped
		 */
		CPRINTS("USART HOST CMD ERROR: Request overrun detected.");
		break;

	case UART_HOST_CMD_RX_BAD:
		/* If state is rx_bad then packet header was bad and process
		 * request was cancelled to drop all incoming bytes.
		 */
		CPRINTS("USART HOST CMD ERROR: Bad packet header detected.");
		break;

	default:
		CPRINTS("USART HOST CMD ERROR: Request timeout mishandled:%d",
			current_state);
	}

	/* Reset host command layer to accept new request */
	uart_host_command_reset();
}
DECLARE_DEFERRED(usart_host_command_request_timeout);

/*
 * This function is called from interrupt handler after entire packet is
 * received.
 */
static void usart_host_command_process_request(void)
{
	/* Handle usart_in_buffer as ec_host_request */
	struct ec_host_request *ec_request =
		(struct ec_host_request *)uart_host_cmd_in_buf;

	/* Prepare host_packet for host command task */
	static struct host_packet uart_packet;

	/*
	 * Disable interrupts before processing request to be sent
	 * to host command task.
	 */
	interrupt_disable();

	/*
	 * In case rx interrupt handler was called in this function's prologue,
	 * host was trying to send extra byte(s) exactly when
	 * UART_DEFERRED_PROCESS_REQ_TIMEOUT expired. If state is
	 * not UART_HOST_CMD_COMPLETE, overrun condition is already
	 * handled.
	 */
	if (current_state != UART_HOST_CMD_COMPLETE) {
		/* Enable interrupts before exiting this function. */
		interrupt_enable();

		return;
	}

	/* Move current_state to UART_HOST_CMD_PROCESSING */
	current_state = UART_HOST_CMD_PROCESSING;

	/* Enable interrupts as current_state is safely handled. */
	interrupt_enable();

	/*
	 * Cancel deferred call to timeout handler as request
	 * received was good.
	 */
	hook_call_deferred(&usart_host_command_request_timeout_data, -1);

	uart_packet.send_response = uart_host_command_process_response;
	uart_packet.request = uart_host_cmd_in_buf;
	uart_packet.request_temp = NULL;
	uart_packet.request_max = sizeof(uart_host_cmd_in_buf);
	uart_packet.request_size = host_request_expected_size(ec_request);
	uart_packet.response = uart_host_cmd_out_buf;
	uart_packet.response_max = sizeof(uart_host_cmd_out_buf);
	uart_packet.response_size = 0;
	uart_packet.driver_result = EC_RES_SUCCESS;

	/* Process usart_packet */
	host_packet_receive(&uart_packet);
}
DECLARE_DEFERRED(usart_host_command_process_request);

/*
 * This function is called from host command task after it is ready with a
 * response.
 */
static void uart_host_command_process_response(struct host_packet *pkt)
{
	/* Disable interrupts before entering critical section. */
	interrupt_disable();

	/*
	 * Send host command response in usart_out_buffer via
	 * tx_interrupt_handler.
	 *
	 * Send response if current state is UART_HOST_CMD_PROCESSING
	 * state. If this layer is in any other state drop response and
	 * let request timeout handler handle state transitions.
	 */
	if (current_state != UART_HOST_CMD_PROCESSING) {
		/* Enable interrupts before exiting critical section. */
		interrupt_enable();

		return;
	}

	/* Move to sending state. */
	current_state = UART_HOST_CMD_SENDING;

	/* Enable interrupts before exiting critical section. */
	interrupt_enable();

	uart_out_datalen = pkt->response_size;
	uart_out_head = 0;

	/* Start sending response to host via usart tx by
	 * triggering tx interrupt.
	 */
	uartn_tx_start(UART_HOST_CMD_HW);
}

void uart_host_command_reset(void)
{
	/* Cancel deferred call to process_request. */
	hook_call_deferred(&usart_host_command_process_request_data, -1);

	/* Cancel deferred call to timeout handler. */
	hook_call_deferred(&usart_host_command_request_timeout_data, -1);

	/*
	 * Disable interrupts before entering critical region
	 * Operations in this section should be minimum to avoid
	 * harming the real-time characteristics of the runtime.
	 */
	interrupt_disable();

	/* Clear in buffer, head and datalen */
	uart_dma_in_head = 0;
	uart_dma_in_head_old = 0;

	/* Clear out buffer, head and datalen */
	uart_out_datalen = 0;
	uart_out_head = 0;

	/* Move to ready state*/
	current_state = UART_HOST_CMD_READY_TO_RX;

	/* Reset UART MDMA module */
	uartn_dma_reset(UART_HOST_CMD_HW);
	uartn_dma_start_rx(UART_HOST_CMD_HW, uart_host_cmd_in_buf,
			   sizeof(uart_host_cmd_in_buf));

	/* Enable interrupts before exiting critical region */
	interrupt_enable();
}

/*
 * Function to handle outgoing bytes from UART interrupt handler
 */
static void uart_host_command_int_handle_tx_data(void)
{
	if (!uartn_nxmip_int_is_enable(UART_HOST_CMD_HW) ||
	    !uartn_tx_ready(UART_HOST_CMD_HW)) {
		return;
	}

	if (uart_out_head != uart_out_datalen) {
		disable_sleep(SLEEP_MASK_UART);
		uartn_write_char(UART_HOST_CMD_HW,
				 uart_host_cmd_out_buf[uart_out_head]);
		uart_out_head++;

	} else {
		uartn_enable_tx_complete_int(UART_HOST_CMD_HW, 0);
		uart_host_command_reset();
		enable_sleep(SLEEP_MASK_UART);
	}
}

/*
 * Function to handle incoming bytes from UART interrupt handler
 */
static void uart_host_command_int_handle_rx_data(void)
{
	/* Once the header is received, store the datalen */
	static int expected_pkg_len;

	/* Define ec_host_request pointer to process in bytes later */
	struct ec_host_request *ec_request =
		(struct ec_host_request *)uart_host_cmd_in_buf;

	uart_dma_in_head = uartn_dma_rx_bytes_done(UART_HOST_CMD_HW);

	if (uart_dma_in_head == uart_dma_in_head_old) {
		return;
	}

	uart_dma_in_head_old = uart_dma_in_head;
	if (current_state == UART_HOST_CMD_READY_TO_RX) {
		/* Kick deferred call to request timeout handler */
		hook_call_deferred(&usart_host_command_request_timeout_data,
				   UART_REQ_RX_TIMEOUT);

		/* Move current state to receiving */
		current_state = UART_HOST_CMD_RECEIVING;
	}

	if (uart_dma_in_head >= sizeof(struct ec_host_request)) {
		/*
		 * Buffer has request header. Check header and get data payload
		 * length.
		 */
		expected_pkg_len = host_request_expected_size(ec_request);

		if (expected_pkg_len == 0 ||
		    expected_pkg_len > UART_HOST_CMD_MAX_REQ_SIZE) {
			/*
			 * EC host request version not compatible or
			 * reserved byte is not zero.
			 */
			current_state = UART_HOST_CMD_RX_BAD;
		} else if (uart_dma_in_head == expected_pkg_len) {
			/*
			 * Once all the datalen bytes are received, wait for
			 * UART_DEFERRED_PROCESS_REQ_TIMEOUT to call
			 * process_request function. This is to catch overrun
			 * bytes before processing the packet.
			 */
			hook_call_deferred(
				&usart_host_command_process_request_data,
				UART_DEFERRED_PROCESS_REQ_TIMEOUT);

			/* If no data in request, packet is complete */
			current_state = UART_HOST_CMD_COMPLETE;
		} else if (uart_dma_in_head > expected_pkg_len) {
			/* Cancel deferred call to process_request */
			hook_call_deferred(
				&usart_host_command_process_request_data, -1);

			/* Move state to overrun*/
			current_state = UART_HOST_CMD_RX_OVERRUN;
		}
	}

	if (current_state == UART_HOST_CMD_PROCESSING)
		/* Host should not send data before receiving a response.
		 * Since the request was already sent to host command task,
		 * just notify console about this. After response is sent
		 * dma will be cleared to handle next packet
		 */
		CPRINTS("USART HOST CMD ERROR: Contiguous packets detected.");
}
/* Interrupt handler for Console UART */
static void uart_host_command_ec_interrupt(void)
{
	uart_host_command_int_handle_tx_data();
	uart_host_command_int_handle_rx_data();
}
DECLARE_IRQ(UART_HOST_COMMAND_IRQ, uart_host_command_ec_interrupt, 2);

void uart_host_command_init(void)
{
	current_state = UART_HOST_CMD_STATE_DISABLED;

	uartn_init(UART_HOST_CMD_HW);
	uartn_dma_init(UART_HOST_CMD_HW);
	uartn_dma_rx_init(UART_HOST_CMD_HW);
	uartn_dma_start_rx(UART_HOST_CMD_HW, uart_host_cmd_in_buf,
			   sizeof(uart_host_cmd_in_buf));

	/* Move to ready state */
	current_state = UART_HOST_CMD_READY_TO_RX;
}

/*
 * Get protocol information
 */
enum ec_status uart_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions |= BIT(3);
	r->max_request_packet_size = UART_HOST_CMD_MAX_REQ_SIZE;
	r->max_response_packet_size = UART_HOST_CMD_MAX_RSP_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
