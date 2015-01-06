/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb.h"
#include "util.h"

/* Size of one USB packet buffer */
#define EP_BUF_SIZE 64
/* Size of the payload (packet minus the header) */
#define EP_PAYLOAD_SIZE (EP_BUF_SIZE - 4)

/* Buffer enough to avoid overflowing due to USB latencies on both sides */
#define RX_COUNT (16 * EP_PAYLOAD_SIZE)

/* Task event for the USB transfer interrupt */
#define USB_EVENTS TASK_EVENT_CUSTOM(3)

/* edge timing samples */
static uint8_t samples[2][RX_COUNT];
/* bitmap of the samples sub-buffer filled with DMA data */
static volatile uint32_t filled_dma;
/* timestamps of the beginning of DMA buffers */
static uint16_t sample_tstamp[4];
/* sequence number of the beginning of DMA buffers */
static uint16_t sample_seq[4];

/* Bulk endpoint double buffer */
static usb_uint ep_buf[2][EP_BUF_SIZE / 2] __usb_ram;
/* USB Buffers not used, ready to be filled */
static volatile uint32_t free_usb = 3;

static inline void led_set_activity(int ch)
{
	static int accumul[2];
	static uint32_t last_ts[2];
	uint32_t now = __hw_clock_source_read();
	int delta = now - last_ts[ch];
	last_ts[ch] = now;
	accumul[ch] = MAX(0, accumul[ch] + (30000 - delta));
	gpio_set_level(ch ? GPIO_LED_R_L : GPIO_LED_G_L, !accumul[ch]);
}

static inline void led_set_record(void)
{
	gpio_set_level(GPIO_LED_B_L, 0);
}

static inline void led_reset_record(void)
{
	gpio_set_level(GPIO_LED_B_L, 1);
}

/* USB descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_VENDOR) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_VENDOR,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceProtocol = 0,
	.iInterface = USB_STR_SNIFFER,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_VENDOR,
						 USB_EP_SNIFFER) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_SNIFFER,
	.bmAttributes = 0x02 /* Bulk IN */,
	.wMaxPacketSize = USB_MAX_PACKET_SIZE,
	.bInterval = 1
};

/* USB callbacks */
static void ep_tx(void)
{
	static int b; /* current buffer index */
	if (btable_ep[USB_EP_SNIFFER].tx_count) {
		/* we have transmitted the previous buffer, toggle it */
		free_usb |= 1 << b;
		b = b ? 0 : 1;
		btable_ep[USB_EP_SNIFFER].tx_addr = usb_sram_addr(ep_buf[b]);
	}
	/* re-enable data transmission if we have available data */
	btable_ep[USB_EP_SNIFFER].tx_count = (free_usb & (1<<b)) ? 0
								 : EP_BUF_SIZE;
	STM32_TOGGLE_EP(USB_EP_SNIFFER, EP_TX_MASK, EP_TX_VALID, 0);
	/* wake up the processing */
	task_set_event(TASK_ID_SNIFFER, 1 << b, 0);
}

static void ep_reset(void)
{
	/* Bulk IN endpoint */
	btable_ep[USB_EP_SNIFFER].tx_addr = usb_sram_addr(ep_buf[0]);
	btable_ep[USB_EP_SNIFFER].tx_count = EP_BUF_SIZE;
	STM32_USB_EP(USB_EP_SNIFFER) = (USB_EP_SNIFFER << 0) /*Endpoint Num*/ |
				       (3 << 4) /* TX Valid */ |
				       (0 << 9) /* Bulk EP */ |
				       (0 << 12) /* RX Disabled */;
}
USB_DECLARE_EP(USB_EP_SNIFFER, ep_tx, ep_tx, ep_reset);


/* --- RX operation using comparator linked to timer --- */
/* RX on CC1 is using COMP1 triggering TIM1 CH1 */
#define TIM_RX1 1
#define DMAC_TIM_RX1 STM32_DMAC_CH6
#define TIM_RX1_CCR_IDX 1
/* RX on CC1 is using COMP2 triggering TIM2 CH4 */
#define TIM_RX2 2
#define DMAC_TIM_RX2 STM32_DMAC_CH7
#define TIM_RX2_CCR_IDX 4

/* Clock divider for RX edges timings (2.4Mhz counter from 48Mhz clock) */
#define RX_CLOCK_DIV (20 - 1)

static const struct dma_option dma_tim_cc1 = {
	DMAC_TIM_RX1, (void *)&STM32_TIM_CCRx(TIM_RX1, TIM_RX1_CCR_IDX),
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	STM32_DMA_CCR_CIRC | STM32_DMA_CCR_TCIE | STM32_DMA_CCR_HTIE
};

static const struct dma_option dma_tim_cc2 = {
	DMAC_TIM_RX2, (void *)&STM32_TIM_CCRx(TIM_RX2, TIM_RX2_CCR_IDX),
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	STM32_DMA_CCR_CIRC | STM32_DMA_CCR_TCIE | STM32_DMA_CCR_HTIE
};

/* sequence number for sample buffers */
static volatile uint32_t seq;
/* Buffer overflow count */
static uint32_t oflow;

void tim_rx1_handler(uint32_t stat)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;
	int idx = !(stat & STM32_DMA_ISR_HTIF(DMAC_TIM_RX1));
	uint32_t mask = idx ? 0xFF00 : 0x00FF;
	uint32_t next = idx ? 0x0001 : 0x0100;

	sample_tstamp[idx] = __hw_clock_source_read();
	sample_seq[idx] = ((seq++ << 3) & 0x0ff8) | (0<<12) /* CC1 */;
	if (filled_dma & next) {
		oflow++;
		sample_seq[idx] |= 0x8000;
	} else {
		led_set_record();
	}
	filled_dma |= mask;
	dma->ifcr = STM32_DMA_ISR_ALL(DMAC_TIM_RX1);
	led_set_activity(0);
}

void tim_rx2_handler(uint32_t stat)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;
	int idx = !(stat & STM32_DMA_ISR_HTIF(DMAC_TIM_RX2));
	uint32_t mask = idx ? 0xFF000000 : 0x00FF0000;
	uint32_t next = idx ? 0x00010000 : 0x01000000;

	idx += 2;
	sample_tstamp[idx] = __hw_clock_source_read();
	sample_seq[idx] = ((seq++ << 3) & 0x0ff8) | (1<<12) /* CC2 */;
	if (filled_dma & next) {
		oflow++;
		sample_seq[idx] |= 0x8000;
	} else {
		led_set_record();
	}
	filled_dma |= mask;
	dma->ifcr = STM32_DMA_ISR_ALL(DMAC_TIM_RX2);
	led_set_activity(1);
}

void tim_dma_handler(void)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;
	uint32_t stat = dma->isr & (STM32_DMA_ISR_HTIF(DMAC_TIM_RX1)
				  | STM32_DMA_ISR_TCIF(DMAC_TIM_RX1)
				  | STM32_DMA_ISR_HTIF(DMAC_TIM_RX2)
				  | STM32_DMA_ISR_TCIF(DMAC_TIM_RX2));
	if (stat & STM32_DMA_ISR_ALL(DMAC_TIM_RX2))
		tim_rx2_handler(stat);
	else
		tim_rx1_handler(stat);
	/* time to process the samples */
	task_set_event(TASK_ID_SNIFFER, TASK_EVENT_CUSTOM(stat), 0);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_4_7, tim_dma_handler, 1);

static void rx_timer_init(int tim_id, timer_ctlr_t *tim, int ch_idx, int up_idx)
{
	int bit_idx = 8 * ((ch_idx - 1) % 2);
	/* --- set counter for RX timing : 2.4Mhz rate, free-running --- */
	__hw_timer_enable_clock(tim_id, 1);
	/* Timer configuration */
	tim->cr1 = 0x0004;
	tim->cr2 = 0x0000;
	/* Auto-reload value : 8-bit free running counter */
	tim->arr = 0xFF;
	/* Counter reloading event after 106us */
	tim->ccr[1] = 0xFF;
	/* Timer ICx input configuration */
	if (ch_idx <= 2)
		tim->ccmr1 = 1 << bit_idx;
	else
		tim->ccmr2 = 1 << bit_idx;
	tim->ccer = 0xB << ((ch_idx - 1) * 4);
	/* TODO: add input filtering */
	/* configure DMA request on CCRx update and overflow/update event */
	tim->dier = (1 << (8 + ch_idx)) | (1 << (8 + up_idx));
	/* set prescaler to /26 (F=2.4Mhz, T=0.4us) */
	tim->psc = RX_CLOCK_DIV;
	/* Reload the pre-scaler and reset the counter, clear CCRx */
	tim->egr = 0x001F;
	/* clear update event from reloading */
	tim->sr = 0;
}

static void sniffer_init(void)
{
	/* remap TIM1 CH1/2/3 to DMA channel 6 */
	STM32_SYSCFG_CFGR1 |= 1 << 28;

	/* TIM1 CH1 for CC1 RX */
	rx_timer_init(TIM_RX1, (void *)STM32_TIM_BASE(TIM_RX1),
		      TIM_RX1_CCR_IDX, 2);
	/* TIM3 CH4 for CC2 RX */
	rx_timer_init(TIM_RX2, (void *)STM32_TIM_BASE(TIM_RX2),
		      TIM_RX2_CCR_IDX, 2);

	/* turn on COMP/SYSCFG */
	STM32_RCC_APB2ENR |= 1 << 0;
	STM32_COMP_CSR = STM32_COMP_CMP1EN | STM32_COMP_CMP1MODE_HSPEED |
			 STM32_COMP_CMP1INSEL_VREF12 |
			 STM32_COMP_CMP1OUTSEL_TIM1_IC1 |
			 STM32_COMP_CMP1HYST_HI |
			 STM32_COMP_CMP2EN | STM32_COMP_CMP2MODE_HSPEED |
			 STM32_COMP_CMP2INSEL_VREF12 |
			 STM32_COMP_CMP2OUTSEL_TIM2_IC4 |
			 STM32_COMP_CMP2HYST_HI;
	ccprintf("Sniffer initialized\n");


	/* start sampling the edges on the CC lines using the RX timers */
	dma_start_rx(&dma_tim_cc1, RX_COUNT, samples[0]);
	dma_start_rx(&dma_tim_cc2, RX_COUNT, samples[1]);
	task_enable_irq(STM32_IRQ_DMA_CHANNEL_4_7);
	/* start RX timers on CC1 and CC2 */
	STM32_TIM_CR1(TIM_RX1) |= 1;
	STM32_TIM_CR1(TIM_RX2) |= 1;
}
DECLARE_HOOK(HOOK_INIT, sniffer_init, HOOK_PRIO_DEFAULT);

/* Task to post-process the samples and copy them the USB endpoint buffer */
void sniffer_task(void)
{
	int u = 0; /* current USB buffer index */
	int d = 0; /* current DMA buffer index */
	int off = 0; /* DMA buffer offset */

	while (1) {
		/* Wait for a new buffer of samples or a new USB free buffer */
		task_wait_event(-1);

		/* send the available samples over USB if we have a buffer*/
		while (filled_dma && free_usb) {
			while (!(filled_dma & (1 << d))) {
				d = (d + 1) & 31;
				off += EP_PAYLOAD_SIZE;
				if (off >= RX_COUNT)
					off = 0;
			}
			ep_buf[u][0] = sample_seq[d >> 3] | (d & 7);
			ep_buf[u][1] = sample_tstamp[d >> 3];
			memcpy_to_usbram(ep_buf[u] + 2,
					 samples[d >> 4]+off, EP_PAYLOAD_SIZE);
			atomic_clear((uint32_t *)&free_usb, 1 << u);
			u = !u;
			atomic_clear((uint32_t *)&filled_dma, 1 << d);
		}
		led_reset_record();
	}
}

int wait_packet(int pol, uint32_t min_edges, uint32_t timeout_us)
{
	stm32_dma_chan_t *chan = dma_get_channel(pol ? DMAC_TIM_RX2
						     : DMAC_TIM_RX1);
	uint32_t t0 = __hw_clock_source_read();
	uint32_t c0 = chan->cndtr;
	uint32_t t_gap = t0;
	uint32_t c_gap = c0;
	uint32_t total_edges = 0;

	while (1) {
		uint32_t t = __hw_clock_source_read();
		uint32_t c = chan->cndtr;
		if (t - t0 > timeout_us) /* Timeout */
			break;
		if (min_edges) { /* real packet detection */
			int nb = (int)c_gap - (int)c;
			if (nb < 0)
				nb = RX_COUNT - nb;
			if (nb > 3) { /* NOT IDLE */
				t_gap = t;
				c_gap = c;
				total_edges += nb;
			} else {
				if ((t - t_gap) > 20 &&
				    (total_edges - (t - t0)/256) >= min_edges)
					/* real gap after the packet */
					break;
			}
		}
	}
	return (__hw_clock_source_read() - t0 > timeout_us);
}

void recording_enable(uint8_t mask)
{
	/* TODO implement */
}

static int command_sniffer(int argc, char **argv)
{
	ccprintf("Seq number:%d Overflows: %d\n", seq, oflow);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sniffer, command_sniffer,
			"[]", "Buffering status", NULL);
