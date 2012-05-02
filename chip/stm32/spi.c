/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI driver for Chrome EC.
 *
 * This uses DMA although not in an optimal way yet.
 */

#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "message.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"


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
 * Our input and output buffers. These must be large enough for our largest
 * message, including protocol overhead.
 */
static char out_msg[32 + MSG_PROTO_BYTES];
static char in_msg[32 + MSG_PROTO_BYTES];

/**
 * Monitor the SPI bus
 *
 * At present this function is very simple - it hangs the system until we
 * have sent the response, then clears things away. This is equivalent to
 * not using DMA at all.
 *
 * TODO(sjg): Use an interrupt on NSS to triggler this function.
 *
 */
void spi_work_task(void)
{
	int port = STM32_SPI1_PORT;

	while (1) {
		task_wait_event(-1);

		/* Wait for the master to let go of our slave select */
		while (!gpio_get_level(GPIO_SPI1_NSS))
			;

		/* Transfer is now complete, so reset everything */
		dma_disable(DMA_CHANNEL_FOR_SPI_RX(port));
		dma_disable(DMA_CHANNEL_FOR_SPI_TX(port));
		STM32_SPI_CR2(port) &= ~CR2_TXDMAEN;
		STM32_SPI_DR(port) = 0xff;
	}
}

/**
 * Send a reply on a given port.
 *
 * The format of a reply is as per the command interface, with a number of
 * preamble bytes before it.
 *
 * The preamble is typically 2 bytes, but can be longer if the STM takes ages
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
 * @param port		Port to send reply back on (STM32_SPI0/1_PORT)
 * @param msg		Message to send
 * @param msg_len	Length of message in bytes
 */
static void reply(int port, char *msg, int msg_len)
{
	int dmac;

	/*
	 * This method is not really suitable for very large messages. If
	 * we need these, we should set up a second DMA transfer to do
	 * the message, and then a third to do the trailer, rather than
	 * copying the message around.
	 */
	STM32_SPI_CR2(port) |= CR2_TXDMAEN;
	dmac = DMA_CHANNEL_FOR_SPI_TX(port);
	dma_start_tx(dmac, msg_len, (void *)&STM32_SPI_DR(port), out_msg);
}

/**
 * Handles an interrupt on the specified port.
 *
 * This signals the start of a transfer. We read the command byte (which is
 * the first byte), star the RX DMA and set up our reply accordingly.
 *
 * We will not get interrupts on subsequent bytes since the DMA will handle
 * the incoming data.
 *
 * @param port	Port that the interrupt came in on (STM32_SPI0/1_PORT)
 */
static void spi_interrupt(int port)
{
	int msg_len;
	char *buff;
	int dmac;
	int cmd;

	/* Make sure there is a byte available */
	if (!(STM32_SPI_SR(port) & SR_RXNE))
		return;

	/* Get the command byte */
	cmd = STM32_SPI_DR(port);

	/* Read the rest of the message - for now we do nothing with it */
	dmac = DMA_CHANNEL_FOR_SPI_RX(port);
	dma_start_rx(dmac, sizeof(in_msg), (void *)&STM32_SPI_DR(port),
		     in_msg);

	/*
	 * Process the command and send the reply. We provide our output
	 * buffer as a suggested location for reply, since this may stop us
	 * needing to copy the message.
	 */
	buff = out_msg;
	msg_len = message_process_cmd(cmd, out_msg, sizeof(out_msg));
	if (msg_len >= 0)
		reply(port, buff, msg_len);

	/* Wake up the task that watches for end of the incoming message */
	task_wake(TASK_ID_SPI_WORK);
}

/* The interrupt code cannot pass a parameters, so handle this here */
static void spi1_interrupt(void) { spi_interrupt(STM32_SPI1_PORT); };

DECLARE_IRQ(STM32_IRQ_SPI1, spi1_interrupt, 2);

static int spi_init(void)
{
	int port;

#if defined(BOARD_daisy)
	/**
	 * SPI1
	 * PA7: SPI1_MOSI
	 * PA6: SPI1_MISO
	 * PA5: SPI1_SCK
	 * PA4: SPI1_NSS
	 *
	 * 8-bit data, master mode, full-duplex, clock is fpclk / 2
	 */
	port = STM32_SPI1_PORT;

	/* enable rx buffer not empty interrupt, and rx DMA */
	STM32_SPI_CR2(port) |= CR2_RXNEIE | CR2_RXDMAEN;

	/* set up an interrupt when we get the first byte of a packet */
	task_enable_irq(STM32_IRQ_SPI1);

	/* write 0xff which will be our default output value */
	STM32_SPI_DR(port) = 0xff;

	/* enable the SPI peripheral */
	STM32_SPI_CR1(port) |= CR1_SPE;
#else
#error "Need to know how to set up SPI for this board"
#endif
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT);
