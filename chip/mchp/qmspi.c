/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* QMSPI master module for MCHP MEC family */

#include "common.h"
#include "console.h"
#include "dma.h"
#include "dma_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "qmspi_chip.h"
#include "registers.h"
#include "spi.h"
#include "spi_chip.h"
#include "task.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ##args)

#define QMSPI_TRANSFER_TIMEOUT (100 * MSEC)
#define QMSPI_BYTE_TRANSFER_TIMEOUT_US (3 * MSEC)
#define QMSPI_BYTE_TRANSFER_POLL_INTERVAL_US 20

#ifndef CONFIG_MCHP_QMSPI_TX_DMA
#ifdef LFW
/*
 * MCHP 32-bit timer 0 configured for 1us count down mode and no
 * interrupt in the LFW environment. Don't need to sleep CPU in LFW.
 */
static int qmspi_wait(uint32_t mask, uint32_t mval)
{
	uint32_t t1, t2, td;

	t1 = MCHP_TMR32_CNT(0);

	while ((MCHP_QMSPI0_STS & mask) != mval) {
		t2 = MCHP_TMR32_CNT(0);
		if (t1 >= t2)
			td = t1 - t2;
		else
			td = t1 + (0xfffffffful - t2);
		if (td > QMSPI_BYTE_TRANSFER_TIMEOUT_US)
			return EC_ERROR_TIMEOUT;
	}
	return EC_SUCCESS;
}
#else
/*
 * This version uses the full EC_RO/RW timer infrastructure and it needs
 * a timer ISR to handle timer underflow. Without the ISR we observe false
 * timeouts when debugging with JTAG.
 * QMSPI_BYTE_TRANSFER_TIMEOUT_US currently 3ms
 * QMSPI_BYTE_TRANSFER_POLL_INTERVAL_US currently 100 us
 */

static int qmspi_wait(uint32_t mask, uint32_t mval)
{
	timestamp_t deadline;

	deadline.val = get_time().val + (QMSPI_BYTE_TRANSFER_TIMEOUT_US);

	while ((MCHP_QMSPI0_STS & mask) != mval) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;

		crec_usleep(QMSPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}
	return EC_SUCCESS;
}
#endif /* #ifdef LFW */
#endif /* #ifndef CONFIG_MCHP_QMSPI_TX_DMA */

/*
 * Wait for QMSPI read using DMA to finish.
 * DMA subsystem has 100 ms timeout
 */
int qmspi_transaction_wait(const struct spi_device_t *spi_device)
{
	const struct dma_option *opdma;

	opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);
	if (opdma != NULL)
		return dma_wait(opdma->channel);

	return EC_ERROR_INVAL;
}

/*
 * Create QMSPI transmit data descriptor not using DMA.
 * Transmit on MOSI pin (single/full-duplex) from TX FIFO.
 * TX FIFO filled by CPU.
 * Caller will apply close and last flags if applicable.
 */
#ifndef CONFIG_MCHP_QMSPI_TX_DMA
static uint32_t qmspi_build_tx_descr(uint32_t ntx, uint32_t ndid)
{
	uint32_t d;

	d = MCHP_QMSPI_C_1X + MCHP_QMSPI_C_TX_DATA;
	d |= ((ndid & 0x0F) << MCHP_QMSPI_C_NEXT_DESCR_BITPOS);

	if (ntx <= MCHP_QMSPI_C_MAX_UNITS)
		d |= MCHP_QMSPI_C_XFRU_1B;
	else {
		if ((ntx & 0x0f) == 0) {
			ntx >>= 4;
			d |= MCHP_QMSPI_C_XFRU_16B;
		} else if ((ntx & 0x03) == 0) {
			ntx >>= 2;
			d |= MCHP_QMSPI_C_XFRU_4B;
		} else
			d |= MCHP_QMSPI_C_XFRU_1B;

		if (ntx > MCHP_QMSPI_C_MAX_UNITS)
			return 0; /* overflow unit count field */
	}

	d |= (ntx << MCHP_QMSPI_C_NUM_UNITS_BITPOS);

	return d;
}

/*
 * Create QMSPI receive data descriptor using DMA.
 * Receive data on MISO pin (single/full-duplex) and store in QMSPI
 * RX FIFO. QMSPI triggers DMA channel to read from RX FIFO and write
 * to memory. Return value is an uint64_t where low 32-bit word is the
 * descriptor and upper 32-bit word is DMA channel unit length with
 * value (1, 2, or 4).
 * Caller will apply close and last flags if applicable.
 */
static uint64_t qmspi_build_rx_descr(uint32_t raddr, uint32_t nrx,
				     uint32_t ndid)
{
	uint32_t d, dmau, na;
	uint64_t u;

	d = MCHP_QMSPI_C_1X + MCHP_QMSPI_C_RX_EN;
	d |= ((ndid & 0x0F) << MCHP_QMSPI_C_NEXT_DESCR_BITPOS);

	dmau = 1;
	na = (raddr | nrx) & 0x03;
	if (na == 0) {
		d |= MCHP_QMSPI_C_RX_DMA_4B;
		dmau <<= 2;
	} else if (na == 0x02) {
		d |= MCHP_QMSPI_C_RX_DMA_2B;
		dmau <<= 1;
	} else {
		d |= MCHP_QMSPI_C_RX_DMA_1B;
	}

	if ((nrx & 0x0f) == 0) {
		nrx >>= 4;
		d |= MCHP_QMSPI_C_XFRU_16B;
	} else if ((nrx & 0x03) == 0) {
		nrx >>= 2;
		d |= MCHP_QMSPI_C_XFRU_4B;
	} else {
		d |= MCHP_QMSPI_C_XFRU_1B;
	}

	u = 0;
	if (nrx <= MCHP_QMSPI_C_MAX_UNITS) {
		d |= (nrx << MCHP_QMSPI_C_NUM_UNITS_BITPOS);
		u = dmau;
		u <<= 32;
		u |= d;
	}

	return u;
}
#endif

#ifdef CONFIG_MCHP_QMSPI_TX_DMA

#define QMSPI_ERR_ANY 0x80
#define QMSPI_ERR_BAD_PTR 0x81
#define QMSPI_ERR_OUT_OF_DESCR 0x85

/*
 * bits[1:0] of word
 * 1 -> 0
 * 2 -> 1
 * 4 -> 2
 */
static uint32_t qmspi_pins_encoding(uint8_t npins)
{
	return (uint32_t)(npins >> 1) & 0x03;
}

/*
 * Clear status, FIFO's, and all descriptors.
 * Enable descriptor mode.
 */
static void qmspi_descr_mode_ready(void)
{
	int i;

	MCHP_QMSPI0_CTRL = 0;
	MCHP_QMSPI0_IEN = 0;
	MCHP_QMSPI0_EXE = MCHP_QMSPI_EXE_CLR_FIFOS;
	MCHP_QMSPI0_STS = 0xfffffffful;
	MCHP_QMSPI0_CTRL = MCHP_QMSPI_C_DESCR_MODE_EN;
	/* clear all descriptors */
	for (i = 0; i < MCHP_QMSPI_MAX_DESCR; i++)
		MCHP_QMSPI0_DESCR(i) = 0;
}

/*
 * helper
 * did = zero based index of start descriptor
 * descr = descriptor configuration
 * nb = number of bytes to transfer
 * Return index of last descriptor allocated or 0xffff
 * if out of descriptors.
 * Algorithm:
 * If requested number of bytes will fit in one descriptor then
 * configure descriptor for QMSPI byte units and return.
 * Otherwise allocate multiple descriptor using QMSPI 16-byte mode
 * and remaining < 16 bytes in byte unit descriptor until all bytes
 * exhausted or out of descriptors error.
 */
static uint32_t qmspi_descr_alloc(uint32_t did, uint32_t descr, uint32_t nb)
{
	uint32_t nu;

	while (nb) {
		if (did >= MCHP_QMSPI_MAX_DESCR)
			return 0xffff;

		descr &=
			~(MCHP_QMSPI_C_NUM_UNITS_MASK + MCHP_QMSPI_C_XFRU_MASK);

		if (nb < (MCHP_QMSPI_C_MAX_UNITS + 1)) {
			descr |= MCHP_QMSPI_C_XFRU_1B;
			descr += (nb << MCHP_QMSPI_C_NUM_UNITS_BITPOS);
			nb = 0;
		} else {
			descr |= MCHP_QMSPI_C_XFRU_16B;
			nu = (nb >> 4) & MCHP_QMSPI_C_NUM_UNITS_MASK0;
			descr += (nu << MCHP_QMSPI_C_NUM_UNITS_BITPOS);
			nb -= (nu << 4);
		}

		descr |= ((did + 1) << MCHP_QMSPI_C_NEXT_DESCR_BITPOS);
		MCHP_QMSPI0_DESCR(did) = descr;
		if (nb)
			did++;
	}

	return did;
}

/*
 * Build one or more descriptors for command/data transmit.
 * cfg b[7:0] = start descriptor index
 * cfg b[15:8] = number of pins for transmit.
 * If bytes to transmit will fit in TX FIFO then fill TX FIFO and build
 * one descriptor.
 * Otherwise build one or more descriptors to fill TX FIFO using DMA
 * channel and configure the DMA channel for memory to device transfer.
 */
static uint32_t qmspi_xmit_data_descr(const struct dma_option *opdma,
				      uint32_t cfg, const uint8_t *data,
				      uint32_t ndata)
{
	uint32_t d, d2, did, dma_cfg;

	did = cfg & 0x0f;
	d = qmspi_pins_encoding((cfg >> 8) & 0x07);

	if (ndata <= MCHP_QMSPI_TX_FIFO_LEN) {
		d2 = d + (ndata << MCHP_QMSPI_C_NUM_UNITS_BITPOS) +
		     MCHP_QMSPI_C_XFRU_1B + MCHP_QMSPI_C_TX_DATA;
		d2 += ((did + 1) << MCHP_QMSPI_C_NEXT_DESCR_BITPOS);
		MCHP_QMSPI0_DESCR(did) = d2;
		while (ndata--)
			MCHP_QMSPI0_TX_FIFO8 = *data++;
	} else { // TX DMA
		if (((uint32_t)data | ndata) & 0x03) {
			dma_cfg = 1;
			d |= (MCHP_QMSPI_C_TX_DATA + MCHP_QMSPI_C_TX_DMA_1B);
		} else {
			dma_cfg = 4;
			d |= (MCHP_QMSPI_C_TX_DATA + MCHP_QMSPI_C_TX_DMA_4B);
		}
		did = qmspi_descr_alloc(did, d, ndata);
		if (did == 0xffff)
			return QMSPI_ERR_OUT_OF_DESCR;

		dma_clr_chan(opdma->channel);
		dma_cfg_buffers(opdma->channel, data, ndata,
				(void *)MCHP_QMSPI0_TX_FIFO_ADDR);
		dma_cfg_xfr(opdma->channel, dma_cfg, MCHP_DMA_QMSPI0_TX_REQ_ID,
			    (DMA_FLAG_M2D + DMA_FLAG_INCR_MEM));
		dma_run(opdma->channel);
	}

	return did;
}

/*
 * QMSPI0 Start
 * flags
 *  b[0] = 1 de-assert chip select when done
 *  b[1] = 1 enable QMSPI interrupts
 *  b[2] = 1 start
 */
void qmspi_cfg_irq_start(uint8_t flags)
{
	MCHP_INT_DISABLE(MCHP_QMSPI_GIRQ) = MCHP_QMSPI_GIRQ_BIT;
	MCHP_INT_SOURCE(MCHP_QMSPI_GIRQ) = MCHP_QMSPI_GIRQ_BIT;
	MCHP_QMSPI0_IEN = 0;

	if (flags & (1u << 1)) {
		MCHP_QMSPI0_IEN =
			(MCHP_QMSPI_STS_DONE + MCHP_QMSPI_STS_PROG_ERR);
		MCHP_INT_ENABLE(MCHP_QMSPI_GIRQ) = MCHP_QMSPI_GIRQ_BIT;
	}

	if (flags & (1u << 2))
		MCHP_QMSPI0_EXE = MCHP_QMSPI_EXE_START;
}

/*
 * QMSPI transmit and/or receive
 * np_flags
 *  b[7:0] = flags
 *	b[0] = close(de-assert chip select when done)
 *	b[1] = enable Done and ProgError interrupt
 *	b[2] = start
 *  b[15:8] = number of tx pins
 *  b[24:16] = number of rx pins
 *
 * returns last descriptor 0 <= index < MCHP_QMSPI_MAX_DESCR
 * or error (bit[7]==1)
 */
uint8_t qmspi_xfr(const struct spi_device_t *spi_device, uint32_t np_flags,
		  const uint8_t *txdata, uint32_t ntx, uint8_t *rxdata,
		  uint32_t nrx)
{
	uint32_t d, did, dma_cfg;
	const struct dma_option *opdma;

	qmspi_descr_mode_ready();

	did = 0;
	if (ntx) {
		if (txdata == NULL)
			return QMSPI_ERR_BAD_PTR;

		opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_WR);

		d = qmspi_pins_encoding((np_flags >> 8) & 0xff);
		dma_cfg = (np_flags & 0xFF00) + did;
		did = qmspi_xmit_data_descr(opdma, dma_cfg, txdata, ntx);
		if (did & QMSPI_ERR_ANY)
			return (uint8_t)(did & 0xff);

		if (nrx)
			did++; /* point to next descriptor */
	}

	if (nrx) {
		if (rxdata == NULL)
			return QMSPI_ERR_BAD_PTR;

		if (did >= MCHP_QMSPI_MAX_DESCR)
			return QMSPI_ERR_OUT_OF_DESCR;

		d = qmspi_pins_encoding((np_flags >> 16) & 0xff);
		/* compute DMA units: 1 or 4 */
		if (((uint32_t)rxdata | nrx) & 0x03) {
			dma_cfg = 1;
			d |= (MCHP_QMSPI_C_RX_EN + MCHP_QMSPI_C_RX_DMA_1B);
		} else {
			dma_cfg = 4;
			d |= (MCHP_QMSPI_C_RX_EN + MCHP_QMSPI_C_RX_DMA_4B);
		}
		did = qmspi_descr_alloc(did, d, nrx);
		if (did & QMSPI_ERR_ANY)
			return (uint8_t)(did & 0xff);

		opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);
		dma_clr_chan(opdma->channel);
		dma_cfg_buffers(opdma->channel, rxdata, nrx,
				(void *)MCHP_QMSPI0_RX_FIFO_ADDR);
		dma_cfg_xfr(opdma->channel, dma_cfg, MCHP_DMA_QMSPI0_RX_REQ_ID,
			    (DMA_FLAG_D2M + DMA_FLAG_INCR_MEM));
		dma_run(opdma->channel);
	}

	if (ntx || nrx) {
		d = MCHP_QMSPI0_DESCR(did);
		d |= MCHP_QMSPI_C_DESCR_LAST;
		if (np_flags & 0x01)
			d |= MCHP_QMSPI_C_CLOSE;
		MCHP_QMSPI0_DESCR(did) = d;
		qmspi_cfg_irq_start(np_flags & 0xFF);
	}

	return (uint8_t)(did & 0xFF);
}
#endif /* #ifdef CONFIG_MCHP_QMSPI_TX_DMA */

/*
 * QMSPI controller must control chip select therefore this routine
 * configures QMSPI to assert SPI CS# and de-assert when done.
 * Transmit using QMSPI TX FIFO only when tx data fits in TX FIFO else
 * use TX DMA.
 * Transmit and receive will allocate as many QMSPI descriptors as
 * needed for data size. This could result in an error if the maximum
 * number of descriptors is exceeded.
 * Descriptors are limited to 0x7FFF units where unit size is 1, 4, or
 * 16 bytes. Code determines unit size based upon number of bytes and
 * alignment of data buffer.
 * DMA channel will move data in units of 1 or 4 bytes also based upon
 * the number of data bytes and buffer alignment.
 * The most efficient transfers are those where TX and RX buffers are
 * aligned >= 4 bytes and the number of bytes is a multiple of 4.
 * NOTE on SPI flash commands:
 * This routine does NOT handle SPI flash commands requiring
 * extra clocks or special mode bytes. Extra clocks and special mode
 * bytes require additional descriptors. For example the flash read
 * dual command (0x3B):
 * 1. First descriptor transmits 4 bytes (opcode + 24-bit address) on
 *    one pin (IO0).
 * 2. Second descriptor set for 2 IO pins, 2 bytes, TX disabled. When
 *    this descriptor is executed QMSPI will tri-state IO0 & IO1 and
 *    output 8 clocks (dual mode 4 clocks per byte). The SPI flash may
 *    turn on its output drivers on the first clock.
 * 3. Third descriptor set for 2 IO pins, read data using DMA. Unit
 *    size and DMA unit size based on number of bytes to read and
 *    alignment of destination buffer.
 * The common SPI API will be required to supply more information about
 * SPI flash read commands.  A further complication is some larger SPI
 * flash devices support a 4-byte address mode. 4-byte address mode can
 * be implemented as separate command code or a configuration bit in
 * the SPI flash that changes the default 24-bit address command to
 * require a 32-bit address.
 * 0x03 is 1-1-1
 * 0x3B is 1-1-2 with 8 clocks
 * 0x6B is 1-1-4 with 8 clocks
 * 0xBB is 1-2-2 with 4 clocks
 * Number of IO pins for command
 * Number of IO pins for address
 * Number of IO pins for data
 * Number of bit/bytes for address (3 or 4)
 * Number of clocks after address phase
 */
#ifdef CONFIG_MCHP_QMSPI_TX_DMA
int qmspi_transaction_async(const struct spi_device_t *spi_device,
			    const uint8_t *txdata, int txlen, uint8_t *rxdata,
			    int rxlen)
{
	uint32_t np_flags, ntx, nrx;
	int ret;
	uint8_t rc;

	ntx = 0;
	if (txlen >= 0)
		ntx = (uint32_t)txlen;

	nrx = 0;
	if (rxlen >= 0)
		nrx = (uint32_t)rxlen;

	np_flags = 0x010105; /* b[0]=1 close on done, b[2]=1 start */
	rc = qmspi_xfr(spi_device, np_flags, txdata, ntx, rxdata, nrx);

	if (rc & QMSPI_ERR_ANY)
		return EC_ERROR_INVAL;

	ret = EC_SUCCESS;
	return ret;
}
#else
/*
 * Transmit using CPU and QMSPI TX FIFO(no DMA).
 * Receive using DMA as above.
 */
int qmspi_transaction_async(const struct spi_device_t *spi_device,
			    const uint8_t *txdata, int txlen, uint8_t *rxdata,
			    int rxlen)
{
	const struct dma_option *opdma;
	uint32_t d, did, dmau;
	uint64_t u;

	if (spi_device == NULL)
		return EC_ERROR_PARAM1;

	/* soft reset the controller */
	MCHP_QMSPI0_MODE_ACT_SRST = MCHP_QMSPI_M_SOFT_RESET;
	d = spi_device->div;
	d <<= MCHP_QMSPI_M_CLKDIV_BITPOS;
	d += (MCHP_QMSPI_M_ACTIVATE + MCHP_QMSPI_M_SPI_MODE0);
	MCHP_QMSPI0_MODE = d;
	MCHP_QMSPI0_CTRL = MCHP_QMSPI_C_DESCR_MODE_EN;

	d = did = 0;

	if (txlen > 0) {
		if (txdata == NULL)
			return EC_ERROR_PARAM2;

		d = qmspi_build_tx_descr((uint32_t)txlen, 1);
		if (d == 0) /* txlen too large */
			return EC_ERROR_OVERFLOW;

		MCHP_QMSPI0_DESCR(did) = d;
	}

	if (rxlen > 0) {
		if (rxdata == NULL)
			return EC_ERROR_PARAM4;

		u = qmspi_build_rx_descr((uint32_t)rxdata, (uint32_t)rxlen, 2);

		d = (uint32_t)u;
		dmau = u >> 32;

		if (txlen > 0)
			did++;
		MCHP_QMSPI0_DESCR(did) = d;

		opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);
		dma_xfr_start_rx(opdma, dmau, (uint32_t)rxlen, rxdata);
	}

	MCHP_QMSPI0_DESCR(did) |=
		(MCHP_QMSPI_C_CLOSE + MCHP_QMSPI_C_DESCR_LAST);

	MCHP_QMSPI0_EXE = MCHP_QMSPI_EXE_START;

	while (txlen--) {
		if (MCHP_QMSPI0_STS & MCHP_QMSPI_STS_TX_BUFF_FULL) {
			if (qmspi_wait(MCHP_QMSPI_STS_TX_BUFF_EMPTY,
				       MCHP_QMSPI_STS_TX_BUFF_EMPTY) !=
			    EC_SUCCESS) {
				MCHP_QMSPI0_EXE = MCHP_QMSPI_EXE_STOP;
				return EC_ERROR_TIMEOUT;
			}
		} else
			MCHP_QMSPI0_TX_FIFO8 = *txdata++;
	}

	return EC_SUCCESS;
}
#endif /* #ifdef CONFIG_MCHP_QMSPI_TX_DMA */

/*
 * Wait for QMSPI descriptor mode transfer to finish.
 * QMSPI is configured to perform a complete transaction.
 * Assert CS#
 * optional transmit
 *   CPU keeps filling TX FIFO until all bytes are transmitted.
 * optional receive
 *   QMSPI is configured to read rxlen bytes and uses a DMA channel
 *   to move data from its RX FIFO to memory.
 * De-assert CS#
 * This routine can be called with QMSPI hardware in four states:
 * 1. Transmit only and QMSPI has finished (empty TX FIFO) by the time
 *    this routine is called. QMSPI.Status transfer done status will be
 *    set and QMSPI HW has de-asserted SPI CS#.
 * 2. Transmit only and QMSPI TX FIFO is still transmitting.
 *    QMSPI transfer done status is not asserted and CS# is still
 *    asserted. QMSPI HW will de-assert CS# when done or firmware
 *    manually stops QMSPI.
 * 3. Receive was enabled and DMA channel is moving data from
 *    QMSPI RX FIFO to memory. QMSPI.Status transfer done and DMA done
 *    status bits are not set. QMSPI SPI CS# will stay asserted until
 *    transaction finishes or firmware manually stops QMSPI.
 * 4. Receive was enabled and DMA channel is finished. QMSPI RX FIFO
 *    should be empty and DMA channel is done.  QMSPI.Status transfer
 *    done and DMA done status bits will be set. QMSPI HW has de-asserted
 *    SPI CS#.
 * We are using QMSPI in descriptor mode. The definition of QMSPI.Status
 * transfer complete bit in this mode is: complete will be set to 1 only
 * when the last buffer completes its transfer.
 * TX only sets complete when transfer unit count is matched and all units
 * have been clocked out of the TX FIFO.
 * RX DMA transfer complete will be set when the last transfer unit
 * is out of the RX FIFO but DMA may not be complete until it finishes
 * moving the transfer unit to memory.
 * If TX only spin on QMSPI.Status Transfer_Complete bit.
 * If RX used spin on QMsPI.Status Transfer_Complete and DMA_Complete.
 * Search descriptors looking for RX DMA enabled.
 * If RX DMA is enabled add DMA complete flag to status mask.
 * Spin while QMSPI.Status & mask != mask or timeout.
 * If timeout force QMSPI to stop and exit spin loop.
 * if DMA was enabled disable DMA channel.
 * Clear QMSPI.Status and FIFO's
 */
int qmspi_transaction_flush(const struct spi_device_t *spi_device)
{
	int ret;
	uint32_t qsts, mask;
	const struct dma_option *opdma;
	timestamp_t deadline;

	if (spi_device == NULL)
		return EC_ERROR_PARAM1;

	mask = MCHP_QMSPI_STS_DONE;

	ret = EC_SUCCESS;
	deadline.val = get_time().val + QMSPI_TRANSFER_TIMEOUT;

	qsts = MCHP_QMSPI0_STS;
	while ((qsts & mask) != mask) {
		if (timestamp_expired(deadline, NULL)) {
			MCHP_QMSPI0_EXE = MCHP_QMSPI_EXE_STOP;
			ret = EC_ERROR_TIMEOUT;
			break;
		}
		crec_usleep(QMSPI_BYTE_TRANSFER_POLL_INTERVAL_US);
		qsts = MCHP_QMSPI0_STS;
	}

	/* clear transmit DMA channel */
	opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_WR);
	if (opdma == NULL)
		return EC_ERROR_INVAL;

	dma_disable(opdma->channel);
	dma_clear_isr(opdma->channel);

	/* clear receive DMA channel */
	opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);
	if (opdma == NULL)
		return EC_ERROR_INVAL;

	dma_disable(opdma->channel);
	dma_clear_isr(opdma->channel);

	/* clear QMSPI FIFO's */
	MCHP_QMSPI0_EXE = MCHP_QMSPI_EXE_CLR_FIFOS;
	MCHP_QMSPI0_STS = 0xffffffff;

	return ret;
}

/**
 * Enable QMSPI controller and MODULE_SPI_FLASH pins.
 *
 * @param hw_port b[3:0]=0 and b[7:4]=0
 * @param enable
 * @return EC_SUCCESS or EC_ERROR_INVAL if port is unrecognized
 * @note called by spi_enable in mec1701/spi.c
 *
 */
int qmspi_enable(int hw_port, int enable)
{
	uint8_t unused __attribute__((unused)) = 0;

	trace2(0, QMSPI, 0, "qmspi_enable: port = %d enable = %d", hw_port,
	       enable);

	if (hw_port != QMSPI0_PORT)
		return EC_ERROR_INVAL;

	gpio_config_module(MODULE_SPI_FLASH, (enable > 0));

	if (enable) {
		MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_QMSPI);
		MCHP_QMSPI0_MODE_ACT_SRST = MCHP_QMSPI_M_SOFT_RESET;
		unused = MCHP_QMSPI0_MODE_ACT_SRST;
		MCHP_QMSPI0_MODE =
			(MCHP_QMSPI_M_ACTIVATE + MCHP_QMSPI_M_SPI_MODE0 +
			 MCHP_QMSPI_M_CLKDIV_12M);
	} else {
		MCHP_QMSPI0_MODE_ACT_SRST = MCHP_QMSPI_M_SOFT_RESET;
		unused = MCHP_QMSPI0_MODE_ACT_SRST;
		MCHP_QMSPI0_MODE_ACT_SRST = 0;
		MCHP_PCR_SLP_EN_DEV(MCHP_PCR_QMSPI);
	}

	return EC_SUCCESS;
}
