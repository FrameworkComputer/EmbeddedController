/*
 * Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI driver for Chrome EC.
 *
 * This uses DMA to handle transmission and reception.
 */

#include "builtin/assert.h"
#include "chipset.h"
#include "clock.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "spi.h"
#include "stm32-dma.h"
#include "system.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SPI, format, ##args)

/* SPI FIFO registers */
#ifdef CHIP_FAMILY_STM32H7
#define SPI_TXDR REG8(&STM32_SPI1_REGS->txdr)
#define SPI_RXDR REG8(&STM32_SPI1_REGS->rxdr)
#else
#define SPI_TXDR STM32_SPI1_REGS->dr
#define SPI_RXDR STM32_SPI1_REGS->dr
#endif

/* DMA channel option */
static const struct dma_option dma_tx_option = {
	STM32_DMAC_SPI1_TX, (void *)&SPI_TXDR,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
#ifdef CHIP_FAMILY_STM32F4
		| STM32_DMA_CCR_CHANNEL(STM32_SPI1_TX_REQ_CH)
#endif
};

static const struct dma_option dma_rx_option = {
	STM32_DMAC_SPI1_RX, (void *)&SPI_RXDR,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
#ifdef CHIP_FAMILY_STM32F4
		| STM32_DMA_CCR_CHANNEL(STM32_SPI1_RX_REQ_CH)
#endif
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
	EC_SPI_PROCESSING, EC_SPI_PROCESSING, EC_SPI_PROCESSING,
	EC_SPI_FRAME_START, /* This is the byte which matters */
};

/*
 * Space allocation of the past-end status byte (EC_SPI_PAST_END) in the out_msg
 * buffer. This seems to be dynamic because the F0 family needs to send it 4
 * times in order to make sure it actually stays at the repeating byte after DMA
 * ends.
 *
 * See crosbug.com/p/31390
 */
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32L4)
#define EC_SPI_PAST_END_LENGTH 4
#else
#define EC_SPI_PAST_END_LENGTH 1
#endif

/*
 * Our input and output buffers. These must be large enough for our largest
 * message, including protocol overhead, and must be 32-bit aligned.
 */
static uint8_t out_msg[SPI_MAX_RESPONSE_SIZE + sizeof(out_preamble) +
		       EC_SPI_PAST_END_LENGTH] __aligned(4) __uncached;
static uint8_t in_msg[SPI_MAX_REQUEST_SIZE] __aligned(4) __uncached;
static uint8_t enabled;
static struct host_packet spi_packet;

/*
 * This is set if SPI NSS raises to high while EC is still processing a
 * command.
 */
static int setup_transaction_later;

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
 * @param nss		GPIO signal for NSS control line
 * @return 0 if bytes received, -1 if we hit a timeout or NSS went high
 */
static int wait_for_bytes(dma_chan_t *rxdma, int needed, enum gpio_signal nss)
{
	timestamp_t deadline;

	ASSERT(needed <= sizeof(in_msg));
	deadline.val = 0;
	while (1) {
		if (dma_bytes_done(rxdma, sizeof(in_msg)) >= needed)
			return 0;
		if (gpio_get_level(nss))
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
 * Sends a byte over SPI without DMA
 *
 * This is mostly used when we want to relay status bytes to the AP while we're
 * receiving the message and we're thinking about it.
 *
 * @note It may be sent 0, 1, or >1 times, depending on whether the host clocks
 * the bus or not. Basically, the EC is saying "if you ask me what my status is,
 * you'll get this value.  But you're not required to ask, or you can ask
 * multiple times."
 *
 * @param byte	status byte to send, one of the EC_SPI_* #defines from
 *		ec_commands.h
 */
static void tx_status(uint8_t byte)
{
	stm32_spi_regs_t *spi __attribute__((unused)) = STM32_SPI1_REGS;

	SPI_TXDR = byte;
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32L4)
	/* It sends the byte 4 times in order to be sure it bypassed the FIFO
	 * from the STM32F0 line.
	 */
	spi->dr = byte;
	spi->dr = byte;
	spi->dr = byte;
#elif defined(CHIP_FAMILY_STM32H7)
	spi->udrdr = byte;
#endif
}

/**
 * Get ready to receive a message from the master.
 *
 * Set up our RX DMA and disable our TX DMA. Set up the data output so that
 * we will send preamble bytes.
 */
static void setup_for_transaction(void)
{
	stm32_spi_regs_t *spi __attribute__((unused)) = STM32_SPI1_REGS;
	volatile uint8_t unused __attribute__((unused));

	/* clear this as soon as possible */
	setup_transaction_later = 0;

#ifndef CHIP_FAMILY_STM32H7 /* H7 is not ready to set status here */
	/* Not ready to receive yet */
	tx_status(EC_SPI_NOT_READY);
#endif

	/* We are no longer actively processing a transaction */
	state = SPI_STATE_PREPARE_RX;

	/* Stop sending response, if any */
	dma_disable(STM32_DMAC_SPI1_TX);

	/*
	 * Read unused bytes in case there are some pending; this prevents the
	 * receive DMA from getting that byte right when we start it.
	 */
	unused = SPI_RXDR;
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32L4)
	/* 4 Bytes makes sure the RX FIFO on the F0 is empty as well. */
	unused = spi->dr;
	unused = spi->dr;
	unused = spi->dr;
#endif

	/* Start DMA */
	dma_start_rx(&dma_rx_option, sizeof(in_msg), in_msg);

	/* Ready to receive */
	state = SPI_STATE_READY_TO_RX;
	tx_status(EC_SPI_RX_READY);

#ifdef CHIP_FAMILY_STM32H7
	spi->cr1 |= STM32_SPI_CR1_SPE;
#endif
}

/* Forward declaration */
static void spi_init(void);

/*
 * If a setup_for_transaction() was postponed, call it now.
 * Note that setup_for_transaction() cancels Tx DMA.
 */
static void check_setup_transaction_later(void)
{
	if (setup_transaction_later) {
		spi_init(); /* Fix for bug chrome-os-partner:31390 */
		/*
		 * 'state' is set to SPI_STATE_READY_TO_RX. Somehow AP
		 * de-asserted the SPI NSS during the handler was running.
		 * Thus, the pending result will be dropped anyway.
		 */
	}
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
	dma_chan_t *txdma;

	/*
	 * If we're not processing, then the AP has already terminated the
	 * transaction, and won't be listening for a response.
	 */
	if (state != SPI_STATE_PROCESSING)
		return;

	/* state == SPI_STATE_PROCESSING */

	/* Append our past-end byte, which we reserved space for. */
	((uint8_t *)pkt->response)[pkt->response_size + 0] = EC_SPI_PAST_END;
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32L4)
	/* Make sure we are going to be outputting it properly when the DMA
	 * ends due to the TX FIFO bug on the F0. See crosbug.com/p/31390
	 */
	((uint8_t *)pkt->response)[pkt->response_size + 1] = EC_SPI_PAST_END;
	((uint8_t *)pkt->response)[pkt->response_size + 2] = EC_SPI_PAST_END;
	((uint8_t *)pkt->response)[pkt->response_size + 3] = EC_SPI_PAST_END;
#endif

	/* Transmit the reply */
	txdma = dma_get_channel(STM32_DMAC_SPI1_TX);
	dma_prepare_tx(&dma_tx_option,
		       sizeof(out_preamble) + pkt->response_size +
			       EC_SPI_PAST_END_LENGTH,
		       out_msg);
	dma_go(txdma);
#ifdef CHIP_FAMILY_STM32H7
	/* clear any previous underrun */
	STM32_SPI1_REGS->ifcr = STM32_SPI_SR_UDR;
#endif /* CHIP_FAMILY_STM32H7 */

	/*
	 * Before the state is set to SENDING, any CS de-assertion would
	 * set setup_transaction_later to 1.
	 */
	state = SPI_STATE_SENDING;
	check_setup_transaction_later();
}

/**
 * Handle an event on the NSS pin
 *
 * A falling edge of NSS indicates that the master is starting a new
 * transaction. A rising edge indicates that we have finished.
 *
 * @param signal	GPIO signal for the NSS pin
 */
void spi_event(enum gpio_signal signal)
{
	dma_chan_t *rxdma;
	uint16_t i;

	/* If not enabled, ignore glitches on NSS */
	if (!enabled)
		return;

	/* Check chip select.  If it's high, the AP ended a transaction. */
	if (gpio_get_level(GPIO_SPI1_NSS)) {
		enable_sleep(SLEEP_MASK_SPI);

		/*
		 * If the buffer is still used by the host command, postpone
		 * the DMA rx setup.
		 */
		if (state == SPI_STATE_PROCESSING) {
			setup_transaction_later = 1;
			return;
		}

		/* Set up for the next transaction */
		spi_init(); /* Fix for bug chrome-os-partner:31390 */
		return;
	}
	disable_sleep(SLEEP_MASK_SPI);

	/* Chip select is low = asserted */
	if (state != SPI_STATE_READY_TO_RX) {
		/*
		 * AP started a transaction but we weren't ready for it.
		 * Tell AP we weren't ready, and ignore the received data.
		 */
		CPRINTS("SPI not ready");
		tx_status(EC_SPI_NOT_READY);
		state = SPI_STATE_RX_BAD;
		return;
	}

	/* We're now inside a transaction */
	state = SPI_STATE_RECEIVING;
	tx_status(EC_SPI_RECEIVING);
	rxdma = dma_get_channel(STM32_DMAC_SPI1_RX);

	/* Wait for version, command, length bytes */
	if (wait_for_bytes(rxdma, 3, GPIO_SPI1_NSS))
		goto spi_event_error;

	if (in_msg[0] == EC_HOST_REQUEST_VERSION) {
		/* Protocol version 3 */
		struct ec_host_request *r = (struct ec_host_request *)in_msg;
		int pkt_size;

		/* Wait for the rest of the command header */
		if (wait_for_bytes(rxdma, sizeof(*r), GPIO_SPI1_NSS))
			goto spi_event_error;

		/*
		 * Check how big the packet should be.  We can't just wait to
		 * see how much data the host sends, because it will keep
		 * sending extra data until we respond.
		 */
		pkt_size = host_request_expected_size(r);
		if (pkt_size == 0 || pkt_size > sizeof(in_msg))
			goto spi_event_error;

		/* Wait for the packet data */
		if (wait_for_bytes(rxdma, pkt_size, GPIO_SPI1_NSS))
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
		spi_packet.response_max = sizeof(out_msg) -
					  sizeof(out_preamble) -
					  EC_SPI_PAST_END_LENGTH;
		spi_packet.response_size = 0;

		spi_packet.driver_result = EC_RES_SUCCESS;

		/* Move to processing state */
		state = SPI_STATE_PROCESSING;
		tx_status(EC_SPI_PROCESSING);

		host_packet_receive(&spi_packet);
		return;

	} else if (in_msg[0] >= EC_CMD_VERSION0) {
		/* Protocol version 2 is deprecated. */
		CPRINTS("ERROR: Protocol V2 is not supported!");
	}

spi_event_error:
	/* Error, timeout, or protocol we can't handle.  Ignore data. */
	tx_status(EC_SPI_RX_BAD_DATA);
	state = SPI_STATE_RX_BAD;
	CPRINTS("SPI rx bad data");

	CPRINTF("in_msg=[");
	for (i = 0; i < dma_bytes_done(rxdma, sizeof(in_msg)); i++)
		CPRINTF("%02x ", in_msg[i]);
	CPRINTF("]\n");
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
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
DECLARE_HOOK(HOOK_CHIPSET_RESUME_INIT, spi_chipset_startup, HOOK_PRIO_DEFAULT);
#else
DECLARE_HOOK(HOOK_CHIPSET_RESUME, spi_chipset_startup, HOOK_PRIO_DEFAULT);
#endif

static void spi_chipset_shutdown(void)
{
	enabled = 0;
	state = SPI_STATE_DISABLED;

	/* Disable pullup and interrupts on NSS */
	gpio_set_flags(GPIO_SPI1_NSS, GPIO_INPUT);

	/* Set SPI pins to inputs so we don't leak power when AP is off */
	gpio_config_module(MODULE_SPI, 0);

	/* Allow deep sleep when AP off */
	enable_sleep(SLEEP_MASK_SPI);
}
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND_COMPLETE, spi_chipset_shutdown,
	     HOOK_PRIO_DEFAULT);
#else
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, spi_chipset_shutdown, HOOK_PRIO_DEFAULT);
#endif

static void spi_init(void)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;
	uint8_t was_enabled = enabled;

	/* Reset the SPI Peripheral to clear any existing weird states. */
	/* Fix for bug chrome-os-partner:31390 */
	enabled = 0;
	state = SPI_STATE_DISABLED;
	STM32_RCC_APB2RSTR |= STM32_RCC_PB2_SPI1;
	STM32_RCC_APB2RSTR &= ~STM32_RCC_PB2_SPI1;

	/* 40 MHz pin speed */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0xff00;

	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;

	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);

	/*
	 * Select the right DMA request for the variants using it.
	 * This is not required for STM32F4 since the channel (aka request) is
	 * set directly in the respective dma_option. In fact, it would be
	 * overridden in dma-stm32f4::prepare_stream().
	 */
#ifdef CHIP_FAMILY_STM32L4
	dma_select_channel(STM32_DMAC_SPI1_TX, 1);
	dma_select_channel(STM32_DMAC_SPI1_RX, 1);
#elif defined(CHIP_FAMILY_STM32H7)
	dma_select_channel(STM32_DMAC_SPI1_TX, DMAMUX1_REQ_SPI1_TX);
	dma_select_channel(STM32_DMAC_SPI1_RX, DMAMUX1_REQ_SPI1_RX);
#endif
	/*
	 * Enable rx/tx DMA and get ready to receive our first transaction and
	 * "disable" FIFO by setting event to happen after only 1 byte
	 */
#ifdef CHIP_FAMILY_STM32H7
	spi->cfg2 = 0;
	spi->cfg1 = STM32_SPI_CFG1_DATASIZE(8) | STM32_SPI_CFG1_FTHLV(4) |
		    STM32_SPI_CFG1_CRCSIZE(8) | STM32_SPI_CFG1_TXDMAEN |
		    STM32_SPI_CFG1_RXDMAEN | STM32_SPI_CFG1_UDRCFG_CONST |
		    STM32_SPI_CFG1_UDRDET_BEGIN_FRM;
	spi->cr1 = 0;
#else /* !CHIP_FAMILY_STM32H7 */
	spi->cr2 = STM32_SPI_CR2_RXDMAEN | STM32_SPI_CR2_TXDMAEN |
		   STM32_SPI_CR2_FRXTH | STM32_SPI_CR2_DATASIZE(8);

	/* Enable the SPI peripheral */
	spi->cr1 |= STM32_SPI_CR1_SPE;
#endif /* !CHIP_FAMILY_STM32H7 */

	gpio_enable_interrupt(GPIO_SPI1_NSS);

	/*
	 * If we were already enabled or chipset is already on,
	 * prepare for transaction
	 */
	if (was_enabled || chipset_in_state(CHIPSET_STATE_ON))
		spi_chipset_startup();
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_INIT_SPI);

/**
 * Get protocol information
 */
enum ec_status spi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions |= BIT(3);
	r->max_request_packet_size = SPI_MAX_REQUEST_SIZE;
	r->max_response_packet_size = SPI_MAX_RESPONSE_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
