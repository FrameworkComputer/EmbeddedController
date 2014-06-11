/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stantum board-specific SPI module */

#include "common.h"
#include "debug.h"
#include "dma.h"
#include "master_slave.h"
#include "registers.h"
#include "spi_comm.h"
#include "task.h"
#include "timer.h"
#include "touch_scan.h"
#include "util.h"

#define DUMMY_DATA 0xdd

/* DMA channel option */
static const struct dma_option dma_tx_option = {
	STM32_DMAC_SPI1_TX, (void *)&STM32_SPI1_REGS->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT
};

static const struct dma_option dma_rx_option = {
	STM32_DMAC_SPI1_RX, (void *)&STM32_SPI1_REGS->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT
};

static uint8_t out_msg[SPI_PACKET_MAX_SIZE + 2];
static uint8_t in_msg[SPI_PACKET_MAX_SIZE];

static inline int wait_for_signal(uint32_t port, uint32_t mask,
				  int value, int timeout_us)
{
	uint32_t start = get_time().le.lo;

	while ((get_time().le.lo - start) < timeout_us) {
		if ((!!(STM32_GPIO_IDR(port) & mask)) == value)
			return EC_SUCCESS;
	}

	return EC_ERROR_TIMEOUT;
}

/*****************************************************************************/
/* Master */

void spi_master_init(void)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	/*
	 * CLK:  AFIO Push-pull
	 * MISO: Input
	 * MOSI: AFIO Push-pull
	 */
	STM32_GPIO_CRL(GPIO_A) = (STM32_GPIO_CRL(GPIO_A) & 0x0ff00fff) |
				 0xb004b000;

	/* Set BR in CR1 */
	spi->cr1 |= STM32_SPI_CR1_BR_DIV4R;

	/* Set CPOL and CPHA */
	/* Use default: 0, 0 */

	/* Set DFF to 8-bit */
	/* Use default: 8-bit */

	/* Configure LSBFIRST */
	/* Use default: MSB first */

	/* Set SSOE */
	/* Use default: software control */

	/* Enable TX and RX DMA */
	spi->cr2 |= STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN;

	/* Set SSM and SSI */
	spi->cr1 |= STM32_SPI_CR1_SSM | STM32_SPI_CR1_SSI;

	/* Enable CRC */
	spi->cr1 |= STM32_SPI_CR1_CRCEN;

	/* Set MSTR and SPE */
	spi->cr1 |= STM32_SPI_CR1_MSTR | STM32_SPI_CR1_SPE;
}

static int spi_master_read_write_byte(uint8_t *in_buf, uint8_t *out_buf, int sz)
{
	int ret;

	dma_start_rx(&dma_rx_option, sz, in_buf);
	dma_prepare_tx(&dma_tx_option, sz, out_buf);
	dma_go(dma_get_channel(STM32_DMAC_SPI1_TX));
	ret = dma_wait(STM32_DMAC_SPI1_TX);
	ret |= dma_wait(STM32_DMAC_SPI1_RX);

	dma_disable(STM32_DMAC_SPI1_TX);
	dma_disable(STM32_DMAC_SPI1_RX);
	dma_clear_isr(STM32_DMAC_SPI1_TX);
	dma_clear_isr(STM32_DMAC_SPI1_RX);

	return ret;
}

int spi_master_send_command(struct spi_comm_packet *cmd)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;
	int ret;

	if (cmd->size + 3 > SPI_PACKET_MAX_SIZE)
		return EC_ERROR_OVERFLOW;

	/* Wait for SPI_NSS to go low */
	if (wait_for_signal(GPIO_A, 1 << 0, 0, 10 * MSEC))
		return EC_ERROR_TIMEOUT;

	/* Set CS1 (slave SPI_NSS) to low */
	STM32_GPIO_BSRR(GPIO_A) = 1 << (6 + 16);

	/* Wait for the slave to acknowledge */
	master_slave_sync(5);

	/* Clock out the packet size. */
	spi->dr = cmd->size;
	while (!(spi->sr & STM32_SPI_SR_RXNE))
		;
	ret = spi->dr;

	/* Wait for the slave to acknowledge */
	master_slave_sync(5);

	/* Clock out command. Don't care about input. */
	ret = spi_master_read_write_byte(in_msg, ((uint8_t *)cmd) + 1,
			cmd->size + SPI_PACKET_HEADER_SIZE - 1);

	return ret;
}

int spi_master_wait_response_async(void)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;
	int size;

	master_slave_sync(40);
	if (wait_for_signal(GPIO_A, 1 << 0, 1, 40 * MSEC))
		goto err_wait_resp_async;

	/* Discard potential garbage in SPI DR */
	if (spi->sr & STM32_SPI_SR_RXNE)
		in_msg[0] = spi->dr;

	/* Get the packet size */
	spi->dr = DUMMY_DATA;
	while (!(spi->sr & STM32_SPI_SR_RXNE))
		;
	in_msg[0] = spi->dr;
	size = in_msg[0] + SPI_PACKET_HEADER_SIZE;

	master_slave_sync(5);

	dma_clear_isr(STM32_DMAC_SPI1_TX);
	dma_clear_isr(STM32_DMAC_SPI1_RX);

	/* Get the rest of the packet*/
	dma_start_rx(&dma_rx_option, size - 1, in_msg + 1);
	dma_prepare_tx(&dma_tx_option, size - 1, out_msg);
	dma_go(dma_get_channel(STM32_DMAC_SPI1_TX));

	return EC_SUCCESS;
err_wait_resp_async:
	/* Set CS1 (slave SPI_NSS) to high */
	STM32_GPIO_BSRR(GPIO_A) = 1 << 6;
	return EC_ERROR_TIMEOUT;
}

const struct spi_comm_packet *spi_master_wait_response_done(void)
{
	const struct spi_comm_packet *resp =
		(const struct spi_comm_packet *)in_msg;
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	if (dma_wait(STM32_DMAC_SPI1_TX) || dma_wait(STM32_DMAC_SPI1_RX)) {
		debug_printf("SPI: Incomplete response\n");
		goto err_wait_response_done;
	}
	if (spi->sr & STM32_SPI_SR_CRCERR) {
		debug_printf("SPI: CRC mismatch\n");
		goto err_wait_response_done;
	}
	if (resp->cmd_sts != EC_SUCCESS) {
		debug_printf("SPI: Slave error\n");
		goto err_wait_response_done;
	}

exit_wait_response_done:
	dma_disable(STM32_DMAC_SPI1_TX);
	dma_disable(STM32_DMAC_SPI1_RX);
	dma_clear_isr(STM32_DMAC_SPI1_TX);
	dma_clear_isr(STM32_DMAC_SPI1_RX);

	/* Set CS1 (slave SPI_NSS) to high */
	STM32_GPIO_BSRR(GPIO_A) = 1 << 6;

	return resp;
err_wait_response_done:
	resp = NULL;
	goto exit_wait_response_done;
}

const struct spi_comm_packet *spi_master_wait_response(void)
{
	if (spi_master_wait_response_async() != EC_SUCCESS)
		return NULL;
	return spi_master_wait_response_done();
}

static uint32_t myrnd(void)
{
	static uint32_t last = 0x1357;
	return last = (last * 8001 + 1);
}

int spi_hello_test(int iteration)
{
	int i, j, xv, sz;
	struct spi_comm_packet *cmd = (struct spi_comm_packet *)out_msg;
	const struct spi_comm_packet *resp;

	for (i = 0; i < iteration; ++i) {
		xv = myrnd() & 0xff;
		cmd->cmd_sts = TS_CMD_HELLO;
		sz = myrnd() % (sizeof(out_msg) - 10) + 1;
		cmd->size = sz + 2;
		cmd->data[0] = sz;
		cmd->data[1] = xv;
		for (j = 0; j < sz; ++j)
			cmd->data[j + 2] = myrnd() & 0xff;
		if (spi_master_send_command(cmd))
			return EC_ERROR_UNKNOWN;

		resp = spi_master_wait_response();
		if (resp == NULL || resp->size != sz)
			return EC_ERROR_UNKNOWN;
		for (j = 0; j < sz; ++j)
			if (cmd->data[j + 2] != resp->data[j])
				return EC_ERROR_UNKNOWN;
		resp = spi_master_wait_response();
		if (resp == NULL || resp->size != sz)
			return EC_ERROR_UNKNOWN;
		for (j = 0; j < sz; ++j)
			if ((cmd->data[j + 2] ^ xv) != resp->data[j])
				return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Slave */

void spi_slave_init(void)
{
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	/*
	 * MISO: AFIO Push-pull
	 */
	STM32_GPIO_CRL(GPIO_A) = (STM32_GPIO_CRL(GPIO_A) & 0xfff0ffff) |
				 0x000b0000;

	/* Set DFF to 8-bit (default) */

	/* Set CPOL and CPHA (default) */

	/* Configure LSBFIRST (default) */

	/* Set SSM and clear SSI */
	spi->cr1 |= STM32_SPI_CR1_SSM;
	spi->cr1 &= ~STM32_SPI_CR1_SSI;

	/* Enable RXNE interrupt */
	spi->cr2 |= STM32_SPI_CR2_RXNEIE;
	/*task_enable_irq(STM32_IRQ_SPI1);*/

	/* Enable TX and RX DMA */
	spi->cr2 |= STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN;

	/* Clear MSTR */
	spi->cr1 &= ~STM32_SPI_CR1_MSTR;

	/* Enable CRC */
	spi->cr1 |= STM32_SPI_CR1_CRCEN;

	/* Set SPE */
	spi->cr1 |= STM32_SPI_CR1_SPE;

	/* Dummy byte to clock out when the next packet comes in */
	spi->dr = DUMMY_DATA;

	/* Enable interrupt on PA0 (GPIO_SPI_NSS) */
	STM32_AFIO_EXTICR(0) &= ~0xF;
	STM32_EXTI_IMR |= (1 << 0);
	task_clear_pending_irq(STM32_IRQ_EXTI0);
	task_enable_irq(STM32_IRQ_EXTI0);
}

int spi_slave_send_response(struct spi_comm_packet *resp)
{
	int r;

	r = spi_slave_send_response_async(resp);
	r |= spi_slave_send_response_flush();

	return r;
}

int spi_slave_send_response_async(struct spi_comm_packet *resp)
{
	int size = resp->size + SPI_PACKET_HEADER_SIZE;
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	if (size > SPI_PACKET_MAX_SIZE)
		return EC_ERROR_OVERFLOW;

	if (out_msg != (uint8_t *)resp)
		memcpy(out_msg, resp, size);

	if (master_slave_sync(100) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (spi->sr & STM32_SPI_SR_RXNE)
		in_msg[0] = spi->dr;
	spi->dr = out_msg[0];

	/* Set N_CHG (master SPI_NSS) to high */
	STM32_GPIO_BSRR(GPIO_A) = 1 << 1;

	while (!(spi->sr & STM32_SPI_SR_RXNE))
		;
	in_msg[0] = spi->dr;

	dma_clear_isr(STM32_DMAC_SPI1_TX);
	dma_clear_isr(STM32_DMAC_SPI1_RX);
	dma_start_rx(&dma_rx_option, size - 1, in_msg);
	dma_prepare_tx(&dma_tx_option, size - 1, out_msg + 1);
	dma_go(dma_get_channel(STM32_DMAC_SPI1_TX));

	if (master_slave_sync(5) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int spi_slave_send_response_flush(void)
{
	int ret;

	ret = dma_wait(STM32_DMAC_SPI1_TX);
	ret |= dma_wait(STM32_DMAC_SPI1_RX);
	dma_disable(STM32_DMAC_SPI1_TX);
	dma_disable(STM32_DMAC_SPI1_RX);
	dma_clear_isr(STM32_DMAC_SPI1_TX);
	dma_clear_isr(STM32_DMAC_SPI1_RX);

	/* Set N_CHG (master SPI_NSS) to low */
	STM32_GPIO_BSRR(GPIO_A) = 1 << (1 + 16);

	return ret;
}

static void spi_slave_nack(void)
{
	struct spi_comm_packet *resp = (struct spi_comm_packet *)out_msg;

	resp->cmd_sts = EC_ERROR_UNKNOWN;
	resp->size = 0;
	spi_slave_send_response(resp);
}

static void spi_slave_hello_back(const struct spi_comm_packet *cmd)
{
	struct spi_comm_packet *resp = (struct spi_comm_packet *)out_msg;
	uint8_t buf[SPI_PACKET_MAX_SIZE];
	int i, sz;

	sz = cmd->data[0];
	memcpy(buf, cmd->data, sz + 2);

	resp->cmd_sts = EC_SUCCESS;
	resp->size = sz;
	for (i = 0; i < sz; ++i)
		resp->data[i] = cmd->data[i + 2];
	spi_slave_send_response(resp);
	for (i = 0; i < sz; ++i)
		resp->data[i] = buf[i + 2] ^ buf[1];
	spi_slave_send_response(resp);
}

static void spi_nss_interrupt(void)
{
	const struct spi_comm_packet *cmd =
		(const struct spi_comm_packet *)in_msg;
	stm32_spi_regs_t *spi = STM32_SPI1_REGS;

	if (spi->sr & STM32_SPI_SR_RXNE)
		in_msg[0] = spi->dr;

	master_slave_sync(5);

	/* Read in the packet size */
	while (!(spi->sr & STM32_SPI_SR_RXNE))
		;
	in_msg[0] = spi->dr;

	/* Read in the rest of the packet */
	dma_clear_isr(STM32_DMAC_SPI1_RX);
	dma_start_rx(&dma_rx_option, in_msg[0] + SPI_PACKET_HEADER_SIZE - 1,
		     in_msg + 1);
	dma_prepare_tx(&dma_tx_option, in_msg[0] + SPI_PACKET_HEADER_SIZE - 1,
		       out_msg);
	dma_go(dma_get_channel(STM32_DMAC_SPI1_TX));

	master_slave_sync(5);

	if (dma_wait(STM32_DMAC_SPI1_RX) != EC_SUCCESS) {
		debug_printf("SPI: Incomplete packet\n");
		spi_slave_nack();
		return;
	}
	if (spi->sr & STM32_SPI_SR_CRCERR) {
		debug_printf("SPI: CRC mismatch\n");
		spi_slave_nack();
		return;
	}

	if (cmd->cmd_sts == TS_CMD_HELLO)
		spi_slave_hello_back(cmd);
	else if (cmd->cmd_sts == TS_CMD_FULL_SCAN)
		touch_scan_slave_start();
	else
		spi_slave_nack();
}

/* Interrupt handler for PA0 */
void IRQ_HANDLER(STM32_IRQ_EXTI0)(void)
{
	/* Clear the interrupt */
	STM32_EXTI_PR = STM32_EXTI_PR;

	/* SPI slave interrupt */
	spi_nss_interrupt();
}

