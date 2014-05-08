/*
 * Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI driver for Chrome EC.
 *
 * This uses DMA to handle transmission and reception.
 */

#include "chipset.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "spi.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTF(format, args...) cprintf(CC_SPI, format, ## args)

/* DMA channel option */
static const struct dma_option dma_tx_option = {
	STM32_DMAC_SPI1_TX, (void *)&STM32_SPI1_REGS->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT
};

static const struct dma_option dma_rx_option = {
	STM32_DMAC_SPI1_RX, (void *)&STM32_SPI1_REGS->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT
};

/*
 * Timeout to wait for SPI request packet
 *
 * This affects the slowest SPI clock we can support.  A delay of 8192 us
 * permits a 512-byte request at 500 KHz, assuming the master starts sending
 * bytes as soon as it asserts chip select.  That's as slow as we would
 * practically want to run the SPI interface, since running it slower
 * significantly impacts firmware update times.
 */
#define SPI_CMD_RX_TIMEOUT_US 8192

/*
 * Offset of output parameters needs to account for pad and framing bytes and
 * one last past-end byte at the end so any additional bytes clocked out by
 * the AP will have a known and identifiable value.
 */
#define SPI_PROTO2_OFFSET (EC_PROTO2_RESPONSE_HEADER_BYTES + 2)
#define SPI_PROTO2_OVERHEAD (SPI_PROTO2_OFFSET +		\
			     EC_PROTO2_RESPONSE_TRAILER_BYTES + 1)

/*
 * Max data size for a version 3 request/response packet.  This is big enough
 * to handle a request/response header, flash write offset/size, and 512 bytes
 * of flash data.
 */
#define SPI_MAX_REQUEST_SIZE 0x220
#define SPI_MAX_RESPONSE_SIZE 0x220

/*
 * The AP blindly clocks back bytes over the SPI interface looking for a
 * framing byte.  So this preamble must always precede the actual response
 * packet.  Search for "spi-frame-header" in U-boot to see how that's
 * implemented.
 *
 * The preamble must be 32-bit aligned so that the response buffer is also
 * 32-bit aligned.
 */
static const uint8_t out_preamble[4] = {
	EC_SPI_PROCESSING,
	EC_SPI_PROCESSING,
	EC_SPI_PROCESSING,
	EC_SPI_FRAME_START,  /* This is the byte which matters */
};

/*
 * Our input and output buffers. These must be large enough for our largest
 * message, including protocol overhead, and must be 32-bit aligned.
 */
static uint8_t out_msg[SPI_MAX_RESPONSE_SIZE + sizeof(out_preamble)]
	__aligned(4);
static uint8_t in_msg[SPI_MAX_REQUEST_SIZE] __aligned(4);
static uint8_t enabled;
static struct host_cmd_handler_args args;
static struct host_packet spi_packet;

enum spi_state {
	/* SPI not enabled (initial state, and when chipset is off) */
	SPI_STATE_DISABLED = 0,

	/* Setting up receive DMA */
	SPI_STATE_PREPARE_RX,

	/* Ready to receive next request */
	SPI_STATE_READY_TO_RX,

	/* Receiving request */
	SPI_STATE_RECEIVING,

	/* Processing request */
	SPI_STATE_PROCESSING,

	/* Sending response */
	SPI_STATE_SENDING,

	/*
	 * Received bad data - transaction started before we were ready, or
	 * packet header from host didn't parse properly.  Ignoring received
	 * data.
	 */
	SPI_STATE_RX_BAD,
} state;

/**
 * Wait until we have received a certain number of bytes
 *
 * Watch the DMA receive channel until it has the required number of bytes,
 * or a timeout occurs
 *
 * We keep an eye on the NSS line - if this goes high then the transaction is
 * over so there is no point in trying to receive the bytes.
 *
 * @param rxdma		RX DMA channel to watch
 * @param needed	Number of bytes that are needed
 * @param nss_regs	GPIO register for NSS control line
 * @param nss_mask	Bit to check in GPIO register (when high, we abort)
 * @return 0 if bytes received, -1 if we hit a timeout or NSS went high
 */
static int wait_for_bytes(stm32_dma_chan_t *rxdma, int needed,
			  uint16_t *nss_reg, uint32_t nss_mask)
{
	timestamp_t deadline;

	ASSERT(needed <= sizeof(in_msg));
	deadline.val = 0;
	while (1) {
		if (dma_bytes_done(rxdma, sizeof(in_msg)) >= needed)
			return 0;
		if (REG16(nss_reg) & nss_mask)
			return -1;
		if (!deadline.val) {
			deadline = get_time();
			deadline.val += SPI_CMD_RX_TIMEOUT_US;
		}
		if (timestamp_expired(deadline, NULL))
			return -1;
	}
}

/**
 * Send a reply on a given port.
 *
 * The format of a reply is as per the command interface, with a number of
 * preamble bytes before it.
 *
 * The format of a reply is a sequence of bytes:
 *
 * <hdr> <status> <len> <msg bytes> <sum> [<preamble byte>...]
 *
 * The hdr byte is just a tag to indicate that the real message follows. It
 * signals the end of any preamble required by the interface.
 *
 * The length is the entire packet size, including the header, length bytes,
 * message payload, checksum, and postamble byte.
 *
 * The preamble is at least 2 bytes, but can be longer if the STM takes ages
 * to react to the incoming message. Since we send our first byte as the AP
 * sends us the command, we clearly can't send anything sensible for that
 * byte. The second byte must be written to the output register just when the
 * command byte is ready (I think), so we can't do anything there either.
 * Any processing we do may increase this delay. That's the reason for the
 * preamble.
 *
 * It is interesting to note that it seems to be possible to run the SPI
 * interface faster than the CPU clock with this approach.
 *
 * We keep an eye on the NSS line - if this goes high then the transaction is
 * over so there is no point in trying to send the reply.
 *
 * @param txdma		TX DMA channel to send on
 * @param status	Status result to send
 * @param msg_ptr	Message payload to send, which normally starts
 *			SPI_PROTO2_OFFSET bytes into out_msg
 * @param msg_len	Number of message bytes to send
 */
static void reply(stm32_dma_chan_t *txdma,
		  enum ec_status status, char *msg_ptr, int msg_len)
{
	char *msg = out_msg;
	int need_copy = msg_ptr != msg + SPI_PROTO2_OFFSET;
	int sum, i;

	ASSERT(msg_len + SPI_PROTO2_OVERHEAD <= sizeof(out_msg));

	/* Add our header bytes - the first one might not actually be sent */
	msg[0] = EC_SPI_PROCESSING;
	msg[1] = EC_SPI_FRAME_START;
	msg[2] = status;
	msg[3] = msg_len & 0xff;

	/*
	 * Calculate the checksum; includes the status and message length bytes
	 * but not the pad and framing bytes since those are stripped by the AP
	 * driver.
	 */
	sum = status + msg_len;
	for (i = 0; i < msg_len; i++) {
		int ch = msg_ptr[i];
		sum += ch;
		if (need_copy)
			msg[i + SPI_PROTO2_OFFSET] = ch;
	}

	/* Add the checksum and get ready to send */
	msg[SPI_PROTO2_OFFSET + msg_len] = sum & 0xff;
	msg[SPI_PROTO2_OFFSET + msg_len + 1] = EC_SPI_PAST_END;
	dma_prepare_tx(&dma_tx_option, msg_len + SPI_PROTO2_OVERHEAD, msg);

	/* Kick off the DMA to send the data */
	dma_go(txdma);
}

/**
 * Get ready to receive a message from the master.
 *
 * Set up our RX DMA and disable our TX DMA. Set up the data output so that
 * we will send preamble bytes.
 */
static void setup_for_transaction(void)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	/* Not ready to receive yet */
	spi->dr = EC_SPI_NOT_READY;

	/* We are no longer actively processing a transaction */
	state = SPI_STATE_PREPARE_RX;

	/* Stop sending response, if any */
	dma_disable(STM32_DMAC_SPI1_TX);

	/*
	 * Read a byte in case there is one pending; this prevents the receive
	 * DMA from getting that byte right when we start it
	 */
	*in_msg = spi->dr;

	/* Start DMA */
	dma_start_rx(&dma_rx_option, sizeof(in_msg), in_msg);

	/* Ready to receive */
	state = SPI_STATE_READY_TO_RX;
	spi->dr = EC_SPI_OLD_READY;
}

/**
 * Called for V2 protocol to indicate that a command has completed
 *
 * Some commands can continue for a while. This function is called by
 * host_command when it completes.
 *
 */
static void spi_send_response(struct host_cmd_handler_args *args)
{
	enum ec_status result = args->result;
	stm32_dma_chan_t *txdma;

	/*
	 * If we're not processing, then the AP has already terminated the
	 * transaction, and won't be listening for a response.
	 */
	if (state != SPI_STATE_PROCESSING)
		return;

	if (args->response_size > args->response_max)
		result = EC_RES_INVALID_RESPONSE;

	/* Transmit the reply */
	txdma = dma_get_channel(STM32_DMAC_SPI1_TX);
	reply(txdma, result, args->response, args->response_size);
}

/**
 * Called to send a response back to the host.
 *
 * Some commands can continue for a while. This function is called by
 * host_command when it completes.
 *
 */
static void spi_send_response_packet(struct host_packet *pkt)
{
	stm32_dma_chan_t *txdma;

	/*
	 * If we're not processing, then the AP has already terminated the
	 * transaction, and won't be listening for a response.
	 */
	if (state != SPI_STATE_PROCESSING)
		return;

	/* Append our past-end byte, which we reserved space for. */
	((uint8_t *)pkt->response)[pkt->response_size] = EC_SPI_PAST_END;

	/* Transmit the reply */
	txdma = dma_get_channel(STM32_DMAC_SPI1_TX);
	dma_prepare_tx(&dma_tx_option,
		       sizeof(out_preamble) + pkt->response_size + 1, out_msg);
	dma_go(txdma);
}

/**
 * Handle an event on the NSS pin
 *
 * A falling edge of NSS indicates that the master is starting a new
 * transaction. A rising edge indicates that we have finsihed
 *
 * @param signal	GPIO signal for the NSS pin
 */
void spi_event(enum gpio_signal signal)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;
	stm32_dma_chan_t *rxdma;
	uint16_t *nss_reg;
	uint32_t nss_mask;

	/* If not enabled, ignore glitches on NSS */
	if (!enabled)
		return;

	/* Check chip select.  If it's high, the AP ended a tranaction. */
	nss_reg = gpio_get_level_reg(GPIO_SPI1_NSS, &nss_mask);
	if (REG16(nss_reg) & nss_mask) {
		/* Set up for the next transaction */
		setup_for_transaction();
		return;
	}

	/* Chip select is low = asserted */
	if (state != SPI_STATE_READY_TO_RX) {
		/*
		 * AP started a transaction but we weren't ready for it.
		 * Tell AP we weren't ready, and ignore the received data.
		 */
		CPRINTF("[%T SPI not ready]\n");
		spi->dr = EC_SPI_NOT_READY;
		state = SPI_STATE_RX_BAD;
		return;
	}

	/* We're now inside a transaction */
	state = SPI_STATE_RECEIVING;
	spi->dr = EC_SPI_RECEIVING;
	rxdma = dma_get_channel(STM32_DMAC_SPI1_RX);

	/* Wait for version, command, length bytes */
	if (wait_for_bytes(rxdma, 3, nss_reg, nss_mask))
		goto spi_event_error;

	if (in_msg[0] == EC_HOST_REQUEST_VERSION) {
		/* Protocol version 3 */
		struct ec_host_request *r = (struct ec_host_request *)in_msg;
		int pkt_size;

		/* Wait for the rest of the command header */
		if (wait_for_bytes(rxdma, sizeof(*r), nss_reg, nss_mask))
			goto spi_event_error;

		/*
		 * Check how big the packet should be.  We can't just wait to
		 * see how much data the host sends, because it will keep
		 * sending dummy data until we respond.
		 */
		pkt_size = host_request_expected_size(r);
		if (pkt_size == 0 || pkt_size > sizeof(in_msg))
			goto spi_event_error;

		/* Wait for the packet data */
		if (wait_for_bytes(rxdma, pkt_size, nss_reg, nss_mask))
			goto spi_event_error;

		spi_packet.send_response = spi_send_response_packet;

		spi_packet.request = in_msg;
		spi_packet.request_temp = NULL;
		spi_packet.request_max = sizeof(in_msg);
		spi_packet.request_size = pkt_size;

		/* Response must start with the preamble */
		memcpy(out_msg, out_preamble, sizeof(out_preamble));
		spi_packet.response = out_msg + sizeof(out_preamble);
		/* Reserve space for the preamble and trailing past-end byte */
		spi_packet.response_max =
			sizeof(out_msg) - sizeof(out_preamble) - 1;
		spi_packet.response_size = 0;

		spi_packet.driver_result = EC_RES_SUCCESS;

		/* Move to processing state */
		state = SPI_STATE_PROCESSING;
		spi->dr = EC_SPI_PROCESSING;

		host_packet_receive(&spi_packet);
		return;

	} else if (in_msg[0] >= EC_CMD_VERSION0) {
		/*
		 * Protocol version 2
		 *
		 * TODO(crosbug.com/p/20257): Remove once kernel supports
		 * version 3.
		 */
		args.version = in_msg[0] - EC_CMD_VERSION0;
		args.command = in_msg[1];
		args.params_size = in_msg[2];

		/* Wait for parameters */
		if (wait_for_bytes(rxdma, 3 + args.params_size,
				   nss_reg, nss_mask))
			goto spi_event_error;

		/*
		 * Params are not 32-bit aligned in protocol version 2.  As a
		 * workaround, move them to the beginning of the input buffer
		 * so they are aligned.
		 */
		if (args.params_size)
			memmove(in_msg, in_msg + 3, args.params_size);

		args.params = in_msg;
		args.send_response = spi_send_response;

		/* Allow room for the header bytes */
		args.response = out_msg + SPI_PROTO2_OFFSET;
		args.response_max = sizeof(out_msg) - SPI_PROTO2_OVERHEAD;
		args.response_size = 0;
		args.result = EC_RES_SUCCESS;

		/* Move to processing state */
		state = SPI_STATE_PROCESSING;
		spi->dr = EC_SPI_PROCESSING;

		host_command_received(&args);
		return;
	}

 spi_event_error:
	/* Error, timeout, or protocol we can't handle.  Ignore data. */
	spi->dr = EC_SPI_RX_BAD_DATA;
	state = SPI_STATE_RX_BAD;
	CPRINTF("[%T SPI rx bad data\n]");
}

static void spi_chipset_startup(void)
{
	/* Enable pullup and interrupts on NSS */
	gpio_set_flags(GPIO_SPI1_NSS, GPIO_INT_BOTH | GPIO_PULL_UP);

	/* Set SPI pins to alternate function */
	gpio_config_module(MODULE_SPI, 1);

	/* Set up for next transaction */
	setup_for_transaction();

	enabled = 1;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, spi_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, spi_chipset_startup, HOOK_PRIO_DEFAULT);

static void spi_chipset_shutdown(void)
{
	enabled = 0;
	state = SPI_STATE_DISABLED;

	/* Disable pullup and interrupts on NSS */
	gpio_set_flags(GPIO_SPI1_NSS, GPIO_INPUT);

	/* Set SPI pins to inputs so we don't leak power when AP is off */
	gpio_config_module(MODULE_SPI, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, spi_chipset_shutdown, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, spi_chipset_shutdown, HOOK_PRIO_DEFAULT);

static void spi_init(void)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	/* 40 MHz pin speed */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0xff00;

	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;

	/* Enable rx DMA and get ready to receive our first transaction */
	spi->cr2 = STM32_SPI_CR2_RXDMAEN | STM32_SPI_CR2_TXDMAEN;

	/* Enable the SPI peripheral */
	spi->cr1 |= STM32_SPI_CR1_SPE;

	gpio_enable_interrupt(GPIO_SPI1_NSS);

	/* If chipset is already on, prepare for transactions */
	if (chipset_in_state(CHIPSET_STATE_ON))
		spi_chipset_startup();
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT);

/**
 * Get protocol information
 */
static int spi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = (1 << 2) | (1 << 3);
	r->max_request_packet_size = SPI_MAX_REQUEST_SIZE;
	r->max_response_packet_size = SPI_MAX_RESPONSE_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     spi_get_protocol_info,
		     EC_VER_MASK(0));
