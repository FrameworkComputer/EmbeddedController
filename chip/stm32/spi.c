/*
 * Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI driver for Chrome EC.
 *
 * This uses DMA to handle transmission and reception.
 */

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
static const struct dma_option dma_tx_option[2] = {
	{DMA_SPI1_TX, (void *)&stm32_spi_addr(STM32_SPI1_PORT)->data,
	 DMA_MSIZE_BYTE | DMA_PSIZE_HALF_WORD},
	{DMA_SPI2_TX, (void *)&stm32_spi_addr(STM32_SPI2_PORT)->data,
	 DMA_MSIZE_BYTE | DMA_PSIZE_HALF_WORD},
};

static const struct dma_option dma_rx_option[2] = {
	{DMA_SPI1_RX, (void *)&stm32_spi_addr(STM32_SPI1_PORT)->data,
	 DMA_MSIZE_BYTE | DMA_PSIZE_HALF_WORD},
	{DMA_SPI2_RX, (void *)&stm32_spi_addr(STM32_SPI2_PORT)->data,
	 DMA_MSIZE_BYTE | DMA_PSIZE_HALF_WORD},
};

/* Status register flags that we use */
enum {
	SR_RXNE		= 1 << 0,
	SR_TXE		= 1 << 1,
	SR_BSY		= 1 << 7,

	CR1_SPE		= 1 << 6,

	CR2_RXDMAEN	= 1 << 0,
	CR2_TXDMAEN	= 1 << 1,
	CR2_RXNEIE	= 1 << 6,
};

/*
 * Since message.c no longer supports our protocol, we must do it all here.
 *
 * We allow a preamble and a header byte so that SPI can function at all.
 * We also add an 8-bit length so that we can tell that we got the whole
 * message, since the master decides how many bytes to read.
 */
enum {
	/* The bytes which appear before the header in a message */
	SPI_MSG_PREAMBLE_BYTE	= 0xff,

	/* The header byte, which follows the preamble */
	SPI_MSG_HEADER_BYTE1	= 0xfe,
	SPI_MSG_HEADER_BYTE2	= 0xec,

	SPI_MSG_HEADER_LEN	= 4,
	SPI_MSG_TRAILER_LEN	= 2,
	SPI_MSG_PROTO_LEN	= SPI_MSG_HEADER_LEN + SPI_MSG_TRAILER_LEN,

	/*
	 * Timeout to wait for SPI command
	 * TODO(sjg@chromium.org): Support much slower SPI clocks. For 4096
	 * we have a delay of 4ms. For the largest message (68 bytes) this
	 * is 130KhZ, assuming that the master starts sending bytes as soon
	 * as it drops NSS. In practice, this timeout seems adequately high
	 * for a 1MHz clock which is as slow as we would reasonably want it.
	 */
	SPI_CMD_RX_TIMEOUT_US	= 8192,
};

/*
 * Our input and output buffers. These must be large enough for our largest
 * message, including protocol overhead.
 */
static uint8_t out_msg[EC_HOST_PARAM_SIZE + SPI_MSG_PROTO_LEN];
static uint8_t in_msg[EC_HOST_PARAM_SIZE + SPI_MSG_PROTO_LEN];
static uint8_t active;
static struct host_cmd_handler_args args;

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
static int wait_for_bytes(struct dma_channel *rxdma, int needed,
			  uint16_t *nss_reg, uint32_t nss_mask)
{
	timestamp_t deadline;

	ASSERT(needed <= sizeof(in_msg));
	deadline.val = 0;
	for (;;) {
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
 * @param spi		SPI controller to send data on
 * @param txdma		TX DMA channel to send on
 * @param status	Status result to send
 * @param msg_ptr	Message payload to send, which normally starts
 *			SPI_MSG_HEADER_LEN bytes into out_msg
 * @param msg_len	Number of message bytes to send
 */
static void reply(struct spi_ctlr *spi, struct dma_channel *txdma,
		  enum ec_status status, char *msg_ptr, int msg_len)
{
	char *msg;
	int sum, i;
	int copy;

	msg = out_msg;
	copy = msg_ptr != msg + SPI_MSG_HEADER_LEN;

	/* Add our header bytes - the first one might not actually be sent */
	msg[0] = SPI_MSG_HEADER_BYTE1;
	msg[1] = SPI_MSG_HEADER_BYTE2;
	msg[2] = status;
	msg[3] = msg_len & 0xff;
	sum = status + msg_len;

	/* Calculate the checksum */
	for (i = 0; i < msg_len; i++) {
		int ch;

		ch = msg_ptr[i];
		sum += ch;
		if (copy)
			msg[i + SPI_MSG_HEADER_LEN] = ch;
	}
	msg_len += SPI_MSG_PROTO_LEN;
	ASSERT(msg_len < sizeof(out_msg));

	/* Add the checksum and get ready to send */
	msg[msg_len - 2] = sum & 0xff;
	msg[msg_len - 1] = SPI_MSG_PREAMBLE_BYTE;
	dma_prepare_tx(dma_tx_option[stm32_spi_port(spi)], msg_len, msg);

	/* Kick off the DMA to send the data */
	dma_go(txdma);
}

/**
 * Get ready to receive a message from the master.
 *
 * Set up our RX DMA and disable our TX DMA. Set up the data output so that
 * we will send preamble bytes.
 */
static void setup_for_transaction(struct spi_ctlr *spi)
{
	int dmac;

	/* We are no longer actively processing a transaction */
	active = 0;

	/* write 0xfd which will be our default output value */
	REG16(&spi->data) = 0xfd;
	dma_disable(DMA_CHANNEL_FOR_SPI_TX(spi));
	*in_msg = 0xff;

	/* read a byte in case there is one, and the rx dma gets it */
	dmac = REG16(&spi->data);
	dmac = DMA_CHANNEL_FOR_SPI_RX(spi);
	dma_start_rx(dma_rx_option[stm32_spi_port(spi)],
		     sizeof(in_msg), in_msg);
}

/**
 * Called to indicate that a command has completed
 *
 * Some commands can continue for a while. This function is called by
 * host_command when it completes.
 *
 */
static void spi_send_response(struct host_cmd_handler_args *args)
{
	struct spi_ctlr *spi;
	enum ec_status result = args->result;
	struct dma_channel *txdma;

	/* If we are too late, don't bother */
	if (!active)
		return;

	spi = (struct spi_ctlr *)stm32_spi_addr(SPI_PORT_HOST);
	if (args->response_size > EC_HOST_PARAM_SIZE)
		result = EC_RES_INVALID_RESPONSE;

	if ((uint8_t *)args->response >= out_msg &&
			(uint8_t *)args->response < out_msg + sizeof(out_msg))
		ASSERT(args->response == out_msg + SPI_MSG_HEADER_LEN);

	/* Transmit the reply */
	txdma = dma_get_channel(DMA_CHANNEL_FOR_SPI_TX(spi));
	reply(spi, txdma, result, args->response, args->response_size);
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
	struct dma_channel *rxdma;
	struct spi_ctlr *spi;
	uint16_t *nss_reg;
	uint32_t nss_mask;

	spi = (struct spi_ctlr *)stm32_spi_addr(SPI_PORT_HOST);

	/*
	 * If NSS is rising, we have finished the transaction, so prepare
	 * for the next.
	 */
	nss_reg = gpio_get_level_reg(GPIO_SPI1_NSS, &nss_mask);
	if (REG16(nss_reg) & nss_mask) {
		setup_for_transaction(spi);
		return;
	}

	active = 1;
	rxdma = dma_get_channel(DMA_CHANNEL_FOR_SPI_RX(spi));

	/* Wait for version, command, length bytes */
	if (wait_for_bytes(rxdma, 3, nss_reg, nss_mask)) {
		setup_for_transaction(spi);
		return;
	}

	if (in_msg[0] >= EC_CMD_VERSION0) {
		args.version = in_msg[0] - EC_CMD_VERSION0;
		args.command = in_msg[1];
		args.params_size = in_msg[2];
		args.params = in_msg + 3;
	} else {
		args.version = 0;
		args.command = in_msg[0];
		args.params = in_msg + 1;
		args.params_size = 0;
		args.version = 0;
	}

	/* Wait for parameters */
	if (wait_for_bytes(rxdma, 3 + args.params_size, nss_reg, nss_mask)) {
		setup_for_transaction(spi);
		return;
	}

	/* Process the command and send the reply */
	args.send_response = spi_send_response;

	/* Allow room for the header bytes */
	args.response = out_msg + SPI_MSG_HEADER_LEN;
	args.response_max = sizeof(out_msg) - SPI_MSG_PROTO_LEN;
	args.response_size = 0;
	args.result = EC_RES_SUCCESS;

	host_command_received(&args);
}

static int spi_init(void)
{
	struct spi_ctlr *spi;

	/* Enable clocks to SPI module */
	STM32_RCC_APB2ENR |= 1 << 12;

	/**
	 * SPI1
	 * PA7: SPI1_MOSI
	 * PA6: SPI1_MISO
	 * PA5: SPI1_SCK
	 * PA4: SPI1_NSS
	 *
	 * 8-bit data, master mode, full-duplex, clock is fpclk / 2
	 */
	spi = (struct spi_ctlr *)stm32_spi_addr(SPI_PORT_HOST);

	/* Enable rx DMA and get ready to receive our first transaction */
	REG16(&spi->ctrl2) = CR2_RXDMAEN | CR2_TXDMAEN;

	/* enable the SPI peripheral */
	REG16(&spi->ctrl1) |= CR1_SPE;

	setup_for_transaction(spi);

	gpio_enable_interrupt(GPIO_SPI1_NSS);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT);
