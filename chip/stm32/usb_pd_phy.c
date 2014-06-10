/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_config.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define PD_DATARATE 300000 /* Hz */

/*
 * Maximum size of a Power Delivery packet (in bits on the wire) :
 *    16-bit header + 0..7 32-bit data objects  (+ 4b5b encoding)
 * 64-bit preamble + SOP (4x 5b) + message in 4b5b + 32-bit CRC + EOP (1x 5b)
 * = 64 + 4*5 + 16 * 5/4 + 7 * 32 * 5/4 + 32 * 5/4 + 5
 */
#define PD_BIT_LEN 429

#define PD_MAX_RAW_SIZE (PD_BIT_LEN*2)

/* maximum number of consecutive similar bits with Biphase Mark Coding */
#define MAX_BITS 2

/* alternating bit sequence used for packet preamble : 00 10 11 01 00 ..  */
#define PD_PREAMBLE 0xB4B4B4B4 /* starts with 0, ends with 1 */

#define TX_CLOCK_DIV ((clock_get_freq() / (2*PD_DATARATE)))

/* threshold for 1 300-khz period */
#define PERIOD 4
#define NB_PERIOD(from, to) ((((to) - (from) + (PERIOD/2)) & 0xFF) / PERIOD)
#define PERIOD_THRESHOLD ((PERIOD + 2*PERIOD) / 2)

/* Timers used for TX and RX clocking */
#define TIM_TX TIM_CLOCK_PD_TX
#define TIM_RX TIM_CLOCK_PD_RX

#include "crc.h"

/* samples for the PD messages */
static uint32_t raw_samples[DIV_ROUND_UP(PD_MAX_RAW_SIZE, sizeof(uint32_t))];

/* state of the bit decoder */
static int d_toggle;
static int d_lastlen;
static uint32_t d_last;

void *pd_init_dequeue(void)
{
	/* preamble ends with 1 */
	d_toggle = 0;
	d_last = 0;
	d_lastlen = 0;

	return raw_samples;
}

static int wait_bits(int nb)
{
	int avail;
	stm32_dma_chan_t *rx = dma_get_channel(DMAC_TIM_RX);

	avail = dma_bytes_done(rx, PD_MAX_RAW_SIZE);
	if (avail < nb) { /* no received yet ... */
		while ((dma_bytes_done(rx, PD_MAX_RAW_SIZE) < nb)
			&& !(STM32_TIM_SR(TIM_RX) & 4))
			; /* optimized for latency, not CPU usage ... */
		if (dma_bytes_done(rx, PD_MAX_RAW_SIZE) < nb) {
			CPRINTS("PD TMOUT RX %d/%d",
				dma_bytes_done(rx, PD_MAX_RAW_SIZE), nb);
			return -1;
		}
	}
	return nb;
}

int pd_dequeue_bits(void *ctxt, int off, int len, uint32_t *val)
{
	int w;
	uint8_t cnt = 0xff;
	uint8_t *samples = ctxt;

	while ((d_lastlen < len) && (off < PD_MAX_RAW_SIZE - 1)) {
		w = wait_bits(off + 2);
		if (w < 0)
			goto stream_err;
		cnt = samples[off] - samples[off-1];
		if (!cnt || (cnt > 3*PERIOD))
			goto stream_err;
		off++;
		if (cnt <= PERIOD_THRESHOLD) {
			/*
			w = wait_bits(off + 1);
			if (w < 0)
				goto stream_err;
			*/
			cnt = samples[off] - samples[off-1];
			if (cnt >  PERIOD_THRESHOLD)
				goto stream_err;
			off++;
		}

		/* enqueue the bit of the last period */
		d_last = (d_last >> 1)
		       | (cnt <= PERIOD_THRESHOLD ? 0x80000000 : 0);
		d_lastlen++;
	}
	if (off < PD_MAX_RAW_SIZE) {
		*val = (d_last << (d_lastlen - len)) >> (32 - len);
		d_lastlen -= len;
		return off;
	} else {
		return -1;
	}
stream_err:
	/* CPRINTS("PD Invalid %d @%d", cnt, off); */
	return -1;
}

int pd_find_preamble(void *ctxt)
{
	int bit;
	uint8_t *vals = ctxt;

	/*
	 * Detect preamble
	 * Alternate 1-period 1-period & 2-period.
	 */
	uint32_t all = 0;
	stm32_dma_chan_t *rx = dma_get_channel(DMAC_TIM_RX);

	for (bit = 1; bit < PD_MAX_RAW_SIZE - 1; bit++) {
		uint8_t cnt;
		/* wait if the bit is not received yet ... */
		if (PD_MAX_RAW_SIZE - rx->cndtr < bit + 1) {
			while ((PD_MAX_RAW_SIZE - rx->cndtr < bit + 1) &&
				!(STM32_TIM_SR(TIM_RX) & 4))
				;
			if (STM32_TIM_SR(TIM_RX) & 4) {
				CPRINTS("PD TMOUT RX %d/%d",
					PD_MAX_RAW_SIZE - rx->cndtr, bit);
				return -1;
			}
		}
		cnt = vals[bit] - vals[bit-1];
		all = (all >> 1) | (cnt <= PERIOD_THRESHOLD ? 1 << 31 : 0);
		if (all == 0x36db6db6)
			return bit - 1; /* should be SYNC-1 */
		if (all == 0xF33F3F3F)
			return -2; /* got HARD-RESET */
	}
	return -1;
}

static int b_toggle;

int pd_write_preamble(void *ctxt)
{
	uint32_t *msg = ctxt;

	/* 64-bit x2 preamble */
	msg[0] = PD_PREAMBLE;
	msg[1] = PD_PREAMBLE;
	msg[2] = PD_PREAMBLE;
	msg[3] = PD_PREAMBLE;
	b_toggle = 0x3FF; /* preamble ends with 1 */
	return 2*64;
}

int pd_write_sym(void *ctxt, int bit_off, uint32_t val10)
{
	uint32_t *msg = ctxt;
	int word_idx = bit_off / 32;
	int bit_idx = bit_off % 32;
	uint32_t val = b_toggle ^ val10;
	b_toggle = val & 0x200 ? 0x3FF : 0;
	if (bit_idx <= 22) {
		if (bit_idx == 0)
			msg[word_idx] = 0;
		msg[word_idx] |= val << bit_idx;
	} else {
		msg[word_idx] |= val << bit_idx;
		msg[word_idx+1] = val >> (32 - bit_idx);
		/* side effect: clear the new word when starting it */
	}
	return bit_off + 5*2;
}

int pd_write_last_edge(void *ctxt, int bit_off)
{
	uint32_t *msg = ctxt;
	int word_idx = bit_off / 32;
	int bit_idx = bit_off % 32;

	if (bit_idx == 0)
		msg[word_idx] = 0;
	if (!b_toggle /* last bit was 0 */) {
		/* transition to 1, then 0 */
		msg[word_idx] |= 1 << bit_idx;
	}
	/* ensure that the trailer is 0 */
	msg[word_idx+1] = 0;

	return bit_off + 2;
}

#ifdef CONFIG_COMMON_RUNTIME
void pd_dump_packet(void *ctxt, const char *msg)
{
	uint8_t *vals = ctxt;
	int bit;

	CPRINTF("ERR %s:\n000:- ", msg);
	/* Packet debug output */
	for (bit = 1; bit <  PD_MAX_RAW_SIZE; bit++) {
		int cnt = NB_PERIOD(vals[bit-1], vals[bit]);
		if ((bit & 31) == 0)
			CPRINTF("\n%03d:", bit);
		CPRINTF("%1d ", cnt);
	}
	CPRINTF("><\n");
	cflush();
	for (bit = 0; bit <  PD_MAX_RAW_SIZE; bit++) {
		if ((bit & 31) == 0)
			CPRINTF("\n%03d:", bit);
		CPRINTF("%02x ", vals[bit]);
	}
	CPRINTF("||\n");
	cflush();
}
#endif /* CONFIG_COMMON_RUNTIME */

/* --- SPI TX operation --- */

static struct dma_option dma_tx_option = {
	DMAC_SPI_TX, (void *)&SPI_REGS->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
};

void pd_tx_set_circular_mode(void)
{
	dma_tx_option.flags |= STM32_DMA_CCR_CIRC;
}

void pd_start_tx(void *ctxt, int polarity, int bit_len)
{
	stm32_dma_chan_t *tx = dma_get_channel(DMAC_SPI_TX);

	/* update DMA configuration */
	dma_prepare_tx(&dma_tx_option, DIV_ROUND_UP(bit_len, 8), ctxt);
	/* Flush data in write buffer so that DMA can get the latest data */
	asm volatile("dmb;");

	/* disable RX detection interrupt */
	pd_rx_disable_monitoring();
	/*
	 * Drive the CC line from the TX block :
	 * - set the low level reference.
	 * - put SPI function on TX pin.
	 */
	pd_tx_enable(polarity);

	/* Kick off the DMA to send the data */
	dma_go(tx);

#ifndef CONFIG_USB_PD_TX_USES_SPI_MASTER
	/* Start counting at 300Khz*/
	STM32_TIM_CR1(TIM_TX) |= 1;
#endif
}

void pd_tx_done(int polarity)
{
	stm32_spi_regs_t *spi = SPI_REGS;

	dma_wait(DMAC_SPI_TX);
	/* wait for real end of transmission */
#ifdef CHIP_FAMILY_STM32F0
	while (spi->sr & STM32_SPI_SR_FTLVL)
		; /* wait for TX FIFO empty */
#else
	while (!(spi->sr & STM32_SPI_SR_TXE))
		; /* wait for TXE == 1 */
#endif

	while (spi->sr & STM32_SPI_SR_BSY)
		; /* wait for BSY == 0 */

	/*
	 * At the end of transmitting, the last bit is guaranteed by the
	 * protocol to be low, and it is necessary that the TX line stay low
	 * until pd_tx_disable().
	 *
	 * When using SPI slave mode for TX, this is done by writing out dummy
	 * 0 byte at end.
	 * When using SPI master mode, the CPOL and CPHA are set high, which
	 * means that after the last bit is transmitted there are no more
	 * clock edges. Hopefully, this is sufficient to guarantee that the
	 * MOSI line does not change before pd_tx_disable().
	 */
#ifndef CONFIG_USB_PD_TX_USES_SPI_MASTER
	/* ensure that we are not pushing out junk */
	*(uint8_t *)&spi->dr = 0;
	/* Stop counting */
	STM32_TIM_CR1(TIM_TX) &= ~1;
#endif
	/* clear transfer flag */
	dma_clear_isr(DMAC_SPI_TX);

	/* put TX pins and reference in Hi-Z */
	pd_tx_disable(polarity);
}

/* --- RX operation using comparator linked to timer --- */

static const struct dma_option dma_tim_option = {
	DMAC_TIM_RX, (void *)&STM32_TIM_CCRx(TIM_RX, TIM_CCR_IDX),
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_16_BIT,
};

void pd_rx_start(void)
{
	/* start sampling the edges on the CC line using the RX timer */
	dma_start_rx(&dma_tim_option, PD_MAX_RAW_SIZE, raw_samples);
	/* enable TIM2 DMA requests */
	STM32_TIM_EGR(TIM_RX) = 0x0001; /* reset counter / reload PSC */;
	STM32_TIM_SR(TIM_RX) = 0; /* clear overflows */
	STM32_TIM_CR1(TIM_RX) |= 1;
}

void pd_rx_complete(void)
{
	/* stop stampling TIM2 */
	STM32_TIM_CR1(TIM_RX) &= ~1;
	/* stop DMA */
	dma_disable(DMAC_TIM_RX);
}

int pd_rx_started(void)
{
	/* is the sampling timer running ? */
	return STM32_TIM_CR1(TIM_RX) & 1;
}

void pd_rx_enable_monitoring(void)
{
	/* clear comparator external interrupt */
	STM32_EXTI_PR = EXTI_COMP_MASK;
	/* clean up older comparator event */
	task_clear_pending_irq(IRQ_COMP);
	/* re-enable comparator interrupt to detect packets */
	task_enable_irq(IRQ_COMP);
}

void pd_rx_disable_monitoring(void)
{
	/* stop monitoring RX during sampling */
	task_disable_irq(IRQ_COMP);
	/* clear comparator external interrupt */
	STM32_EXTI_PR = EXTI_COMP_MASK;
}

/* detect an edge on the PD RX pin */
void pd_rx_handler(void)
{
	/* start sampling */
	pd_rx_start();
	/* ignore the comparator IRQ until we are done with current message */
	pd_rx_disable_monitoring();
	/* trigger the analysis in the task */
	pd_rx_event();
}
#ifndef BOARD_ZINGER
DECLARE_IRQ(STM32_IRQ_COMP, pd_rx_handler, 1);
#endif

/* --- Startup initialization --- */
void *pd_hw_init(void)
{
	stm32_spi_regs_t *spi = SPI_REGS;

	/* set 40 MHz pin speed on communication pins */
	pd_set_pins_speed();

	/* --- SPI init --- */

	/* Enable clocks to SPI module */
	spi_enable_clock();

	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();

	/* Enable Tx DMA for our first transaction */
	spi->cr2 = STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_DATASIZE(8);

#ifdef CONFIG_USB_PD_TX_USES_SPI_MASTER
	/*
	 * Enable the master SPI: LSB first, force NSS, TX only, CPOL and CPHA
	 * high.
	 */
	spi->cr1 = STM32_SPI_CR1_LSBFIRST | STM32_SPI_CR1_BIDIMODE
		 | STM32_SPI_CR1_SSM | STM32_SPI_CR1_SSI
		 | STM32_SPI_CR1_BIDIOE | STM32_SPI_CR1_MSTR
		 | STM32_SPI_CR1_BR_DIV64R | STM32_SPI_CR1_SPE
		 | STM32_SPI_CR1_CPOL | STM32_SPI_CR1_CPHA;

#if CPU_CLOCK != 38400000
#error "CPU_CLOCK must be 38.4MHz to use SPI master for USB PD Tx"
#endif
#else
	/* Enable the slave SPI: LSB first, force NSS, TX only */
	spi->cr1 = STM32_SPI_CR1_SPE | STM32_SPI_CR1_LSBFIRST
		 | STM32_SPI_CR1_SSM | STM32_SPI_CR1_BIDIMODE
		 | STM32_SPI_CR1_BIDIOE;
#endif

	/* configure TX DMA */
	dma_prepare_tx(&dma_tx_option, PD_MAX_RAW_SIZE, raw_samples);

#ifndef CONFIG_USB_PD_TX_USES_SPI_MASTER
	/* --- set the TX timer with updates at 600KHz (BMC frequency) --- */
	__hw_timer_enable_clock(TIM_TX, 1);
	/* Timer configuration */
	STM32_TIM_CR1(TIM_TX) = 0x0000;
	STM32_TIM_CR2(TIM_TX) = 0x0000;
	STM32_TIM_DIER(TIM_TX) = 0x0000;
	/* Auto-reload value : 600000 Khz overflow */
	STM32_TIM_ARR(TIM_TX) = TX_CLOCK_DIV;
	/* 50% duty cycle on the output */
	STM32_TIM_CCR1(TIM_TX) = STM32_TIM_ARR(TIM_TX) / 2;
	/* Timer CH1 output configuration */
	STM32_TIM_CCMR1(TIM_TX) = (6 << 4) | (1 << 3);
	STM32_TIM_CCER(TIM_TX) = 1;
	STM32_TIM_BDTR(TIM_TX) = 0x8000;
	/* set prescaler to /1 */
	STM32_TIM_PSC(TIM_TX) = 0;
	/* Reload the pre-scaler and reset the counter */
	STM32_TIM_EGR(TIM_TX) = 0x0001;
#endif

	/* --- set counter for RX timing : 2.4Mhz rate, free-running --- */
	__hw_timer_enable_clock(TIM_RX, 1);
	/* Timer configuration */
	STM32_TIM_CR1(TIM_RX) = 0x0000;
	STM32_TIM_CR2(TIM_RX) = 0x0000;
	STM32_TIM_DIER(TIM_RX) = 0x0000;
	/* Auto-reload value : 16-bit free running counter */
	STM32_TIM_ARR(TIM_RX) = 0xFFFF;

	/* Timeout for message receive : 2.7ms */
	STM32_TIM_CCR2(TIM_RX) = 2400000 * 27 / 10000;
	/* Timer ICx input configuration */
#if TIM_CCR_IDX == 1
	STM32_TIM_CCMR1(TIM_RX) = TIM_CCR_CS << 0;
#elif TIM_CCR_IDX == 4
	STM32_TIM_CCMR2(TIM_RX) = TIM_CCR_CS << 8;
#else
#error Unsupported RX timer capture input
#endif
	STM32_TIM_CCER(TIM_RX) = 0xB << ((TIM_CCR_IDX - 1) * 4);
	/* configure DMA request on CCRx update */
	STM32_TIM_DIER(TIM_RX) |= 1 << (8 + TIM_CCR_IDX); /* CCxDE */;
	/* set prescaler to /26 (F=1.2Mhz, T=0.8us) */
	STM32_TIM_PSC(TIM_RX) = (clock_get_freq() / 2400000) - 1;
	/* Reload the pre-scaler and reset the counter */
	STM32_TIM_EGR(TIM_RX) = 0x0001 | (1 << TIM_CCR_IDX) /* clear CCRx */;
	/* clear update event from reloading */
	STM32_TIM_SR(TIM_RX) = 0;

	/* --- DAC configuration for comparator at 850mV --- */
#ifdef CONFIG_PD_USE_DAC_AS_REF
	/* Enable DAC interface clock. */
	STM32_RCC_APB1ENR |= (1 << 29);
	/* set voltage Vout=0.850V (Vref = 3.0V) */
	STM32_DAC_DHR12RD = 850 * 4096 / 3000;
	/* Start DAC channel 1 */
	STM32_DAC_CR = STM32_DAC_CR_EN1;
#endif

	/* --- COMP2 as comparator for RX vs Vmid = 850mV --- */
#ifdef CONFIG_USB_PD_INTERNAL_COMP
#if defined(CHIP_FAMILY_STM32F0)
	/* 40 MHz pin speed on PA0 and PA4 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x303;
	/* turn on COMP/SYSCFG */
	STM32_RCC_APB2ENR |= 1 << 0;
	/* currently in hi-speed mode : TODO revisit later, INM = PA0(INM6) */
	STM32_COMP_CSR = STM32_COMP_CMP1MODE_LSPEED |
			 STM32_COMP_CMP1INSEL_INM6 |
			 STM32_COMP_CMP1OUTSEL_TIM1_IC1 |
			 STM32_COMP_CMP1HYST_HI |
			 STM32_COMP_CMP2MODE_LSPEED |
			 STM32_COMP_CMP2INSEL_INM6 |
			 STM32_COMP_CMP2OUTSEL_TIM1_IC1 |
			 STM32_COMP_CMP2HYST_HI;
#elif defined(CHIP_FAMILY_STM32L)
	/* 40 MHz pin speed on PB4 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x300;

	STM32_RCC_APB1ENR |= 1 << 31; /* turn on COMP */

	STM32_COMP_CSR = STM32_COMP_OUTSEL_TIM2_IC4 | STM32_COMP_INSEL_DAC_OUT1
			| STM32_COMP_SPEED_FAST;
	/* route PB4 to COMP input2 through GR6_1 bit 4 (or PB5->GR6_2 bit 5) */
	STM32_RI_ASCR2 |= 1 << 4;
#else
#error Unsupported chip family
#endif
#endif /* CONFIG_USB_PD_INTERNAL_COMP */
	/* DBG */usleep(250000);
	/* comparator interrupt setup */
	EXTI_XTSR |= EXTI_COMP_MASK;
	STM32_EXTI_IMR |= EXTI_COMP_MASK;
	task_enable_irq(IRQ_COMP);

	CPRINTS("USB PD initialized");
	return raw_samples;
}

void pd_set_clock(int freq)
{
	STM32_TIM_ARR(TIM_TX) = clock_get_freq() / (2*freq);
}
