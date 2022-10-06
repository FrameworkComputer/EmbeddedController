/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "builtin/assert.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "crc.h"
#include "dma.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_config.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
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

#define PD_MAX_RAW_SIZE (PD_BIT_LEN * 2)

/* maximum number of consecutive similar bits with Biphase Mark Coding */
#define MAX_BITS 2

/* alternating bit sequence used for packet preamble : 00 10 11 01 00 ..  */
#define PD_PREAMBLE 0xB4B4B4B4 /* starts with 0, ends with 1 */

#define TX_CLOCK_DIV ((clock_get_freq() / (2 * PD_DATARATE)))

/* threshold for 1 300-khz period */
#define PERIOD 4
#define NB_PERIOD(from, to) ((((to) - (from) + (PERIOD / 2)) & 0xFF) / PERIOD)
#define PERIOD_THRESHOLD ((PERIOD + 2 * PERIOD) / 2)

static struct pd_physical {
	/* samples for the PD messages */
	uint32_t raw_samples[DIV_ROUND_UP(PD_MAX_RAW_SIZE, sizeof(uint32_t))];

	/* state of the bit decoder */
	int d_toggle;
	int d_lastlen;
	uint32_t d_last;
	int b_toggle;

	/* DMA structures for each PD port */
	struct dma_option dma_tx_option;
	struct dma_option dma_tim_option;

	/* Pointers to timer register for each port */
	timer_ctlr_t *tim_tx;
	timer_ctlr_t *tim_rx;
} pd_phy[CONFIG_USB_PD_PORT_MAX_COUNT];

/* keep track of RX edge timing in order to trigger receive */
static timestamp_t rx_edge_ts[CONFIG_USB_PD_PORT_MAX_COUNT]
			     [PD_RX_TRANSITION_COUNT];
static int rx_edge_ts_idx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* keep track of transmit polarity for DMA interrupt */
static int tx_dma_polarities[CONFIG_USB_PD_PORT_MAX_COUNT];

void pd_init_dequeue(int port)
{
	/* preamble ends with 1 */
	pd_phy[port].d_toggle = 0;
	pd_phy[port].d_last = 0;
	pd_phy[port].d_lastlen = 0;
}

static int wait_bits(int port, int nb)
{
	int avail;
	stm32_dma_chan_t *rx = dma_get_channel(DMAC_TIM_RX(port));

	avail = dma_bytes_done(rx, PD_MAX_RAW_SIZE);
	if (avail < nb) { /* no received yet ... */
		while ((dma_bytes_done(rx, PD_MAX_RAW_SIZE) < nb) &&
		       !(pd_phy[port].tim_rx->sr & 4))
			; /* optimized for latency, not CPU usage ... */
		if (dma_bytes_done(rx, PD_MAX_RAW_SIZE) < nb) {
			CPRINTS("PD TMOUT RX %d/%d",
				dma_bytes_done(rx, PD_MAX_RAW_SIZE), nb);
			return -1;
		}
	}
	return nb;
}

int pd_dequeue_bits(int port, int off, int len, uint32_t *val)
{
	int w;
	uint8_t cnt = 0xff;
	uint8_t *samples = (uint8_t *)pd_phy[port].raw_samples;

	while ((pd_phy[port].d_lastlen < len) && (off < PD_MAX_RAW_SIZE - 1)) {
		w = wait_bits(port, off + 2);
		if (w < 0)
			goto stream_err;
		cnt = samples[off] - samples[off - 1];
		if (!cnt || (cnt > 3 * PERIOD))
			goto stream_err;
		off++;
		if (cnt <= PERIOD_THRESHOLD) {
			/*
			w = wait_bits(port, off + 1);
			if (w < 0)
				goto stream_err;
			*/
			cnt = samples[off] - samples[off - 1];
			if (cnt > PERIOD_THRESHOLD)
				goto stream_err;
			off++;
		}

		/* enqueue the bit of the last period */
		pd_phy[port].d_last =
			(pd_phy[port].d_last >> 1) |
			(cnt <= PERIOD_THRESHOLD ? 0x80000000 : 0);
		pd_phy[port].d_lastlen++;
	}
	if (off < PD_MAX_RAW_SIZE) {
		*val = (pd_phy[port].d_last
			<< (pd_phy[port].d_lastlen - len)) >>
		       (32 - len);
		pd_phy[port].d_lastlen -= len;
		return off;
	} else {
		return -1;
	}
stream_err:
	/* CPRINTS("PD Invalid %d @%d", cnt, off); */
	return -1;
}

int pd_find_preamble(int port)
{
	int bit;
	uint8_t *vals = (uint8_t *)pd_phy[port].raw_samples;

	/*
	 * Detect preamble
	 * Alternate 1-period 1-period & 2-period.
	 */
	uint32_t all = 0;
	stm32_dma_chan_t *rx = dma_get_channel(DMAC_TIM_RX(port));

	for (bit = 1; bit < PD_MAX_RAW_SIZE - 1; bit++) {
		uint8_t cnt;
		/* wait if the bit is not received yet ... */
		if (PD_MAX_RAW_SIZE - rx->cndtr < bit + 1) {
			while ((PD_MAX_RAW_SIZE - rx->cndtr < bit + 1) &&
			       !(pd_phy[port].tim_rx->sr & 4))
				;
			if (pd_phy[port].tim_rx->sr & 4) {
				CPRINTS("PD TMOUT RX %d/%d",
					PD_MAX_RAW_SIZE - rx->cndtr, bit);
				return -1;
			}
		}
		cnt = vals[bit] - vals[bit - 1];
		all = (all >> 1) | (cnt <= PERIOD_THRESHOLD ? BIT(31) : 0);
		if (all == 0x36db6db6)
			return bit - 1; /* should be SYNC-1 */
		if (all == 0xF33F3F3F)
			return PD_RX_ERR_HARD_RESET; /* got HARD-RESET */
		if (all == 0x3c7fe0ff)
			return PD_RX_ERR_CABLE_RESET; /* got CABLE-RESET */
	}
	return -1;
}

int pd_write_preamble(int port)
{
	uint32_t *msg = pd_phy[port].raw_samples;

	/* 64-bit x2 preamble */
	msg[0] = PD_PREAMBLE;
	msg[1] = PD_PREAMBLE;
	msg[2] = PD_PREAMBLE;
	msg[3] = PD_PREAMBLE;
	pd_phy[port].b_toggle = 0x3FF; /* preamble ends with 1 */
	return 2 * 64;
}

int pd_write_sym(int port, int bit_off, uint32_t val10)
{
	uint32_t *msg = pd_phy[port].raw_samples;
	int word_idx = bit_off / 32;
	int bit_idx = bit_off % 32;
	uint32_t val = pd_phy[port].b_toggle ^ val10;
	pd_phy[port].b_toggle = val & 0x200 ? 0x3FF : 0;
	if (bit_idx <= 22) {
		if (bit_idx == 0)
			msg[word_idx] = 0;
		msg[word_idx] |= val << bit_idx;
	} else {
		msg[word_idx] |= val << bit_idx;
		msg[word_idx + 1] = val >> (32 - bit_idx);
		/* side effect: clear the new word when starting it */
	}
	return bit_off + 5 * 2;
}

int pd_write_last_edge(int port, int bit_off)
{
	uint32_t *msg = pd_phy[port].raw_samples;
	int word_idx = bit_off / 32;
	int bit_idx = bit_off % 32;

	if (bit_idx == 0)
		msg[word_idx] = 0;

	if (!pd_phy[port].b_toggle /* last bit was 0 */) {
		/* transition to 1, another 1, then 0 */
		if (bit_idx == 31) {
			msg[word_idx++] |= 1 << bit_idx;
			msg[word_idx] = 1;
		} else {
			msg[word_idx] |= 3 << bit_idx;
		}
	}
	/* ensure that the trailer is 0 */
	msg[word_idx + 1] = 0;

	return bit_off + 3;
}

#ifdef CONFIG_COMMON_RUNTIME
void pd_dump_packet(int port, const char *msg)
{
	uint8_t *vals = (uint8_t *)pd_phy[port].raw_samples;
	int bit;

	CPRINTF("ERR %s:\n000:- ", msg);
	/* Packet debug output */
	for (bit = 1; bit < PD_MAX_RAW_SIZE; bit++) {
		int cnt = NB_PERIOD(vals[bit - 1], vals[bit]);
		if ((bit & 31) == 0)
			CPRINTF("\n%03d:", bit);
		CPRINTF("%1d ", cnt);
	}
	CPRINTF("><\n");
	cflush();
	for (bit = 0; bit < PD_MAX_RAW_SIZE; bit++) {
		if ((bit & 31) == 0)
			CPRINTF("\n%03d:", bit);
		CPRINTF("%02x ", vals[bit]);
	}
	CPRINTF("||\n");
	cflush();
}
#endif /* CONFIG_COMMON_RUNTIME */

/* --- SPI TX operation --- */

void pd_tx_spi_init(int port)
{
	stm32_spi_regs_t *spi = SPI_REGS(port);

	/* Enable Tx DMA for our first transaction */
	spi->cr2 = STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_DATASIZE(8);

	/* Enable the slave SPI: LSB first, force NSS, TX only, CPHA */
	spi->cr1 = STM32_SPI_CR1_SPE | STM32_SPI_CR1_LSBFIRST |
		   STM32_SPI_CR1_SSM | STM32_SPI_CR1_BIDIMODE |
		   STM32_SPI_CR1_BIDIOE | STM32_SPI_CR1_CPHA;
}

static void tx_dma_done(void *data)
{
	int port = (int)data;
	int polarity = tx_dma_polarities[port];
	stm32_spi_regs_t *spi = SPI_REGS(port);

	while (spi->sr & STM32_SPI_SR_FTLVL)
		; /* wait for TX FIFO empty */
	while (spi->sr & STM32_SPI_SR_BSY)
		; /* wait for BSY == 0 */

	/* Stop counting */
	pd_phy[port].tim_tx->cr1 &= ~1;

	/* put TX pins and reference in Hi-Z */
	pd_tx_disable(port, polarity);

#if defined(CONFIG_COMMON_RUNTIME) && defined(CONFIG_DMA_DEFAULT_HANDLERS)
	task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_DMA_TC);
#endif
}

int pd_start_tx(int port, int polarity, int bit_len)
{
	stm32_dma_chan_t *tx = dma_get_channel(DMAC_SPI_TX(port));

#ifndef CONFIG_USB_PD_TX_PHY_ONLY
	/* disable RX detection interrupt */
	pd_rx_disable_monitoring(port);

	/* Check that we are not receiving a frame to avoid collisions */
	if (pd_rx_started(port))
		return -5;
#endif /* !CONFIG_USB_PD_TX_PHY_ONLY */

	/* Initialize spi peripheral to prepare for transmission. */
	pd_tx_spi_init(port);

	/*
	 * Set timer to one tick before reset so that the first tick causes
	 * a rising edge on the output.
	 */
	pd_phy[port].tim_tx->cnt = TX_CLOCK_DIV - 1;

	/* update DMA configuration */
	dma_prepare_tx(&(pd_phy[port].dma_tx_option), DIV_ROUND_UP(bit_len, 8),
		       pd_phy[port].raw_samples);
	/* Flush data in write buffer so that DMA can get the latest data */
	asm volatile("dmb;");

	/* Kick off the DMA to send the data */
	dma_clear_isr(DMAC_SPI_TX(port));
#if defined(CONFIG_COMMON_RUNTIME) && defined(CONFIG_DMA_DEFAULT_HANDLERS)
	tx_dma_polarities[port] = polarity;
	if (!(pd_phy[port].dma_tx_option.flags & STM32_DMA_CCR_CIRC)) {
		/* Only enable interrupt if not in circular mode */
		dma_enable_tc_interrupt_callback(DMAC_SPI_TX(port),
						 &tx_dma_done, (void *)port);
	}
#endif
	dma_go(tx);

	/*
	 * Drive the CC line from the TX block :
	 * - put SPI function on TX pin.
	 * - set the low level reference.
	 * Call this last before enabling timer in order to meet spec on
	 * timing between enabling TX and clocking out bits.
	 */
	pd_tx_enable(port, polarity);

	/* Start counting at 300Khz*/
	pd_phy[port].tim_tx->cr1 |= 1;

	return bit_len;
}

void pd_tx_done(int port, int polarity)
{
#if defined(CONFIG_COMMON_RUNTIME) && defined(CONFIG_DMA_DEFAULT_HANDLERS)
	/* wait for DMA, DMA interrupt will stop the SPI clock */
	task_wait_event_mask(TASK_EVENT_DMA_TC, DMA_TRANSFER_TIMEOUT_US);
	dma_disable_tc_interrupt(DMAC_SPI_TX(port));
#else
	tx_dma_polarities[port] = polarity;
	tx_dma_done((void *)port);
#endif

	/* Reset SPI to clear remaining data in buffer */
	pd_tx_spi_reset(port);
}

void pd_tx_set_circular_mode(int port)
{
	pd_phy[port].dma_tx_option.flags |= STM32_DMA_CCR_CIRC;
}

void pd_tx_clear_circular_mode(int port)
{
	/* clear the circular mode bit in flag variable */
	pd_phy[port].dma_tx_option.flags &= ~STM32_DMA_CCR_CIRC;
	/* disable dma transaction underway */
	dma_disable(DMAC_SPI_TX(port));
#if defined(CONFIG_COMMON_RUNTIME) && defined(CONFIG_DMA_DEFAULT_HANDLERS)
	tx_dma_done((void *)port);
#endif
}

/* --- RX operation using comparator linked to timer --- */

void pd_rx_start(int port)
{
	/* start sampling the edges on the CC line using the RX timer */
	dma_start_rx(&(pd_phy[port].dma_tim_option), PD_MAX_RAW_SIZE,
		     pd_phy[port].raw_samples);
	/* enable TIM2 DMA requests */
	pd_phy[port].tim_rx->egr = 0x0001; /* reset counter / reload PSC */
	;
	pd_phy[port].tim_rx->sr = 0; /* clear overflows */
	pd_phy[port].tim_rx->cr1 |= 1;
}

void pd_rx_complete(int port)
{
	/* stop stampling TIM2 */
	pd_phy[port].tim_rx->cr1 &= ~1;
	/* stop DMA */
	dma_disable(DMAC_TIM_RX(port));
}

int pd_rx_started(int port)
{
	/* is the sampling timer running ? */
	return pd_phy[port].tim_rx->cr1 & 1;
}

void pd_rx_enable_monitoring(int port)
{
	/* clear comparator external interrupt */
	STM32_EXTI_PR = EXTI_COMP_MASK(port);
	/* enable comparator external interrupt */
	STM32_EXTI_IMR |= EXTI_COMP_MASK(port);
}

void pd_rx_disable_monitoring(int port)
{
	/* disable comparator external interrupt */
	STM32_EXTI_IMR &= ~EXTI_COMP_MASK(port);
	/* clear comparator external interrupt */
	STM32_EXTI_PR = EXTI_COMP_MASK(port);
}

uint64_t get_time_since_last_edge(int port)
{
	int prev_idx = (rx_edge_ts_idx[port] == 0) ?
			       PD_RX_TRANSITION_COUNT - 1 :
			       rx_edge_ts_idx[port] - 1;
	return get_time().val - rx_edge_ts[port][prev_idx].val;
}

/* detect an edge on the PD RX pin */
void pd_rx_handler(void)
{
	int pending, i;
	int next_idx;
	pending = STM32_EXTI_PR;

#ifdef CONFIG_USB_CTVPD
	/* Charge-Through Side detach event */
	if (pending & EXTI_COMP2_MASK) {
		task_wake(PD_PORT_TO_TASK_ID(0));
		/* Clear interrupt */
		STM32_EXTI_PR = EXTI_COMP2_MASK;
		pending &= ~EXTI_COMP2_MASK;
	}
#endif

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (pending & EXTI_COMP_MASK(i)) {
			rx_edge_ts[i][rx_edge_ts_idx[i]].val = get_time().val;
			next_idx = (rx_edge_ts_idx[i] ==
				    PD_RX_TRANSITION_COUNT - 1) ?
					   0 :
					   rx_edge_ts_idx[i] + 1;

#if defined(CONFIG_LOW_POWER_IDLE) && \
	defined(CONFIG_USB_PD_LOW_POWER_IDLE_WHEN_CONNECTED)
			/*
			 * Do not deep sleep while waiting for more edges. For
			 * most boards, sleep is already disabled due to being
			 * in PD connected state, but boards which define
			 * CONFIG_USB_PD_LOW_POWER_IDLE_WHEN_CONNECTED can
			 * sleep while connected.
			 */
			disable_sleep(SLEEP_MASK_USB_PD);
#endif

			/*
			 * If we have seen enough edges in a certain amount of
			 * time, then trigger RX start.
			 */
			if ((rx_edge_ts[i][rx_edge_ts_idx[i]].val -
			     rx_edge_ts[i][next_idx].val) <
			    PD_RX_TRANSITION_WINDOW) {
				/* start sampling */
				pd_rx_start(i);
				/*
				 * ignore the comparator IRQ until we are done
				 * with current message
				 */
				pd_rx_disable_monitoring(i);
				/* trigger the analysis in the task */
				pd_rx_event(i);
			} else {
				/* do not trigger RX start, just clear int */
				STM32_EXTI_PR = EXTI_COMP_MASK(i);
			}
			rx_edge_ts_idx[i] = next_idx;
		}
	}
}
#ifdef CONFIG_USB_PD_RX_COMP_IRQ
static void _pd_rx_handler(void)
{
	pd_rx_handler();
}
DECLARE_IRQ(STM32_IRQ_COMP, _pd_rx_handler, 1);
#endif

/* --- release hardware --- */
void pd_hw_release(int port)
{
	__hw_timer_enable_clock(TIM_CLOCK_PD_RX(port), 0);
	__hw_timer_enable_clock(TIM_CLOCK_PD_TX(port), 0);
	dma_disable(DMAC_SPI_TX(port));
}

/* --- Startup initialization --- */

void pd_hw_init_rx(int port)
{
	struct pd_physical *phy = &pd_phy[port];

	/* configure registers used for timers */
	phy->tim_rx = (void *)TIM_REG_RX(port);

	/* configure RX DMA */
	phy->dma_tim_option.channel = DMAC_TIM_RX(port);
	phy->dma_tim_option.periph = (void *)(TIM_RX_CCR_REG(port));
	phy->dma_tim_option.flags = STM32_DMA_CCR_MSIZE_8_BIT |
				    STM32_DMA_CCR_PSIZE_16_BIT;

	/* --- set counter for RX timing : 2.4Mhz rate, free-running --- */
	__hw_timer_enable_clock(TIM_CLOCK_PD_RX(port), 1);
	/* Timer configuration */
	phy->tim_rx->cr1 = 0x0000;
	phy->tim_rx->cr2 = 0x0000;
	phy->tim_rx->dier = 0x0000;
	/* Auto-reload value : 16-bit free running counter */
	phy->tim_rx->arr = 0xFFFF;

	/* Timeout for message receive */
	phy->tim_rx->ccr[2] = (2400000 / 1000) * USB_PD_RX_TMOUT_US / 1000;
	/* Timer ICx input configuration */
	if (TIM_RX_CCR_IDX(port) == 1)
		phy->tim_rx->ccmr1 |= TIM_CCR_CS << 0;
	else if (TIM_RX_CCR_IDX(port) == 2)
		phy->tim_rx->ccmr1 |= TIM_CCR_CS << 8;
	else if (TIM_RX_CCR_IDX(port) == 4)
		phy->tim_rx->ccmr2 |= TIM_CCR_CS << 8;
	else
		/*  Unsupported RX timer capture input */
		ASSERT(0);

	phy->tim_rx->ccer = 0xB << ((TIM_RX_CCR_IDX(port) - 1) * 4);
	/* configure DMA request on CCRx update */
	phy->tim_rx->dier |= 1 << (8 + TIM_RX_CCR_IDX(port)); /* CCxDE */
	;
	/* set prescaler to /26 (F=1.2Mhz, T=0.8us) */
	phy->tim_rx->psc = (clock_get_freq() / 2400000) - 1;
	/* Reload the pre-scaler and reset the counter (clear CCRx) */
	phy->tim_rx->egr = 0x0001 | (1 << TIM_RX_CCR_IDX(port));
	/* clear update event from reloading */
	phy->tim_rx->sr = 0;

	/* --- DAC configuration for comparator at 850mV --- */
#ifdef CONFIG_PD_USE_DAC_AS_REF
	/* Enable DAC interface clock. */
	STM32_RCC_APB1ENR |= BIT(29);
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
	/* set voltage Vout=0.850V (Vref = 3.0V) */
	STM32_DAC_DHR12RD = 850 * 4096 / 3000;
	/* Start DAC channel 1 */
	STM32_DAC_CR = STM32_DAC_CR_EN1;
#endif

	/* --- COMP2 as comparator for RX vs Vmid = 850mV --- */
#ifdef CONFIG_USB_PD_INTERNAL_COMP
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	/* turn on COMP/SYSCFG */
	STM32_RCC_APB2ENR |= BIT(0);
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
	/* currently in hi-speed mode : TODO revisit later, INM = PA0(INM6) */
	STM32_COMP_CSR = STM32_COMP_CMP1MODE_LSPEED |
			 STM32_COMP_CMP1INSEL_INM6 | CMP1OUTSEL |
			 STM32_COMP_CMP1HYST_HI | STM32_COMP_CMP2MODE_LSPEED |
			 STM32_COMP_CMP2INSEL_INM6 | CMP2OUTSEL |
			 STM32_COMP_CMP2HYST_HI;
#elif defined(CHIP_FAMILY_STM32L)
	STM32_RCC_APB1ENR |= BIT(31); /* turn on COMP */

	STM32_COMP_CSR = STM32_COMP_OUTSEL_TIM2_IC4 |
			 STM32_COMP_INSEL_DAC_OUT1 | STM32_COMP_SPEED_FAST;
	/* route PB4 to COMP input2 through GR6_1 bit 4 (or PB5->GR6_2 bit 5) */
	STM32_RI_ASCR2 |= BIT(4);
#else
#error Unsupported chip family
#endif
#endif /* CONFIG_USB_PD_INTERNAL_COMP */

	/* comparator interrupt setup */
	EXTI_XTSR |= EXTI_COMP_MASK(port);
	STM32_EXTI_IMR |= EXTI_COMP_MASK(port);
	task_enable_irq(IRQ_COMP);
}

void pd_hw_init(int port, enum pd_power_role role)
{
	struct pd_physical *phy = &pd_phy[port];
	uint32_t val;

	/* Initialize all PD pins to default state based on desired role */
	pd_config_init(port, role);

	/* set 40 MHz pin speed on communication pins */
	pd_set_pins_speed(port);

	/* --- SPI init --- */

	/* Enable clocks to SPI module */
	spi_enable_clock(port);

	/* Initialize SPI peripheral registers */
	pd_tx_spi_init(port);

	/* configure TX DMA */
	phy->dma_tx_option.channel = DMAC_SPI_TX(port);
	phy->dma_tx_option.periph = (void *)&SPI_REGS(port)->dr;
	phy->dma_tx_option.flags = STM32_DMA_CCR_MSIZE_8_BIT |
				   STM32_DMA_CCR_PSIZE_8_BIT;
	dma_prepare_tx(&(phy->dma_tx_option), PD_MAX_RAW_SIZE,
		       phy->raw_samples);

	/* configure registers used for timers */
	phy->tim_tx = (void *)TIM_REG_TX(port);

	/* --- set the TX timer with updates at 600KHz (BMC frequency) --- */
	__hw_timer_enable_clock(TIM_CLOCK_PD_TX(port), 1);
	/* Timer configuration */
	phy->tim_tx->cr1 = 0x0000;
	phy->tim_tx->cr2 = 0x0000;
	phy->tim_tx->dier = 0x0000;
	/* Auto-reload value : 600000 Khz overflow */
	phy->tim_tx->arr = TX_CLOCK_DIV;
	/* 50% duty cycle on the output */
	phy->tim_tx->ccr[TIM_TX_CCR_IDX(port)] = phy->tim_tx->arr / 2;
	/* Timer channel output configuration */
	val = (6 << 4) | BIT(3);
	if ((TIM_TX_CCR_IDX(port) & 1) == 0) /* CH2 or CH4 */
		val <<= 8;
	if (TIM_TX_CCR_IDX(port) <= 2)
		phy->tim_tx->ccmr1 = val;
	else
		phy->tim_tx->ccmr2 = val;

	phy->tim_tx->ccer = 1 << ((TIM_TX_CCR_IDX(port) - 1) * 4);
	phy->tim_tx->bdtr = 0x8000;
	/* set prescaler to /1 */
	phy->tim_tx->psc = 0;
	/* Reload the pre-scaler and reset the counter */
	phy->tim_tx->egr = 0x0001;
#ifndef CONFIG_USB_PD_TX_PHY_ONLY
	/* Configure the reception side : comparators + edge timer + DMA */
	pd_hw_init_rx(port);
#endif /* CONFIG_USB_PD_TX_PHY_ONLY */

	CPRINTS("USB PD initialized");
}

void pd_set_clock(int port, int freq)
{
	pd_phy[port].tim_tx->arr = clock_get_freq() / (2 * freq);
}
