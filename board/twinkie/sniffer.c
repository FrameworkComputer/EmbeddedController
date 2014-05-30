/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#define RX_COUNT (8 * EP_PAYLOAD_SIZE)

/* Task event for the USB transfer interrupt */
#define USB_EVENTS TASK_EVENT_CUSTOM(3)

/* edge timing samples */
static uint8_t samples[RX_COUNT];

/* Bulk endpoint double buffer */
static usb_uint ep_buf[2][EP_BUF_SIZE / 2] __usb_ram;

/* The host is reading the incoming packets */
static int tx_on;

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
	int b = !(STM32_USB_EP(USB_EP_SNIFFER) & EP_TX_DTOG);
	if (b)
		btable_ep[USB_EP_SNIFFER].rx_count = 0;
	else
		btable_ep[USB_EP_SNIFFER].tx_count = 0;
	/* wake up the processing */
	task_set_event(TASK_ID_SNIFFER, 1 << b, 0);
	/* clear IT, toggle and validate buffers */
	STM32_TOGGLE_EP(USB_EP_SNIFFER, 0, 0, EP_RX_DTOG);
	STM32_TOGGLE_EP(USB_EP_SNIFFER, EP_TX_MASK, EP_TX_VALID, 0);
	/* record the valid transmission */
	tx_on = 1;
}

static void ep_reset(void)
{
	/* Bulk IN endpoint with double-buffering */
	btable_ep[USB_EP_SNIFFER].tx_addr = usb_sram_addr(ep_buf[0]);
	btable_ep[USB_EP_SNIFFER].rx_addr = usb_sram_addr(ep_buf[1]);
	btable_ep[USB_EP_SNIFFER].tx_count = 0;
	btable_ep[USB_EP_SNIFFER].rx_count = 0;
	STM32_USB_EP(USB_EP_SNIFFER) = (USB_EP_SNIFFER << 0) /*Endpoint Num*/ |
				       (3 << 4) /* TX Valid */ |
				       (0 << 9) /* Bulk EP */ |
				       (1 << 8) /* DBL BUF */ |
				       (0 << 12) /* RX Disabled */;
}
USB_DECLARE_EP(USB_EP_SNIFFER, ep_tx, ep_tx, ep_reset);


/* --- RX operation using comparator linked to timer --- */
/* RX is using COMP1 triggering TIM1 CH1 */
#define DMAC_TIM_RX STM32_DMAC_CH6
#define TIM_CCR_IDX 1
#define TIM_CCR_CS  1
#define EXTI_COMP 21

/* Timer used for RX clocking */
#define TIM_RX TIM_CLOCK_PD_RX
/* Clock divider for RX edges timings (2.4Mhz counter from 48Mhz clock) */
#define RX_CLOCK_DIV (20 - 1)

static const struct dma_option dma_tim_option = {
	DMAC_TIM_RX, (void *)&STM32_TIM_CCRx(TIM_RX, TIM_CCR_IDX),
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	STM32_DMA_CCR_CIRC | STM32_DMA_CCR_TCIE | STM32_DMA_CCR_HTIE
};

/* sequence number for sample buffers */
static volatile uint32_t seq;
/* Buffer overflow count */
static uint32_t oflow;

void tim_dma_handler(void)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;
	uint32_t stat = dma->isr & (STM32_DMA_ISR_HTIF(DMAC_TIM_RX)
				  | STM32_DMA_ISR_TCIF(DMAC_TIM_RX));
	seq++;
	dma->ifcr |= STM32_DMA_ISR_ALL(DMAC_TIM_RX);
	if (tx_on)
		task_set_event(TASK_ID_SNIFFER, TASK_EVENT_CUSTOM(stat), 0);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_4_7, tim_dma_handler, 1);

static void sniffer_init(void)
{
	/* remap TIM1 CH1/2/3 to DMA channel 6 */
	STM32_SYSCFG_CFGR1 |= 1 << 28;
	/* --- set counter for RX timing : 2.4Mhz rate, free-running --- */
	__hw_timer_enable_clock(TIM_RX, 1);
	/* Timer configuration */
	STM32_TIM_CR1(TIM_RX) = 0x0000;
	STM32_TIM_CR2(TIM_RX) = 0x0000;
	/* Auto-reload value : 8-bit free running counter */
	STM32_TIM_ARR(TIM_RX) = 0xFF;
	/* Counter reloading event after 106us */
	STM32_TIM_CCR2(TIM_RX) = 0xFF;
	/* Timer ICx input configuration */
#if TIM_CCR_IDX == 1
	STM32_TIM_CCMR1(TIM_RX) = TIM_CCR_CS << 0;
#elif TIM_CCR_IDX == 4
	STM32_TIM_CCMR2(TIM_RX) = TIM_CCR_CS << 8;
#else
#error Unsupported RX timer capture input
#endif
	STM32_TIM_CCER(TIM_RX) = 0xB << ((TIM_CCR_IDX - 1) * 4);
	/* TODO: add input filtering */
	/* configure DMA request on CCRx update */
	STM32_TIM_DIER(TIM_RX) = (1 << (8 + TIM_CCR_IDX)) | (1 << (8+2));
	/* set prescaler to /26 (F=2.4Mhz, T=0.4us) */
	STM32_TIM_PSC(TIM_RX) = RX_CLOCK_DIV;
	/* Reload the pre-scaler and reset the counter */
	STM32_TIM_EGR(TIM_RX) = 0x0001 | (1 << TIM_CCR_IDX) /* clear CCRx */;
	/* clear update event from reloading */
	STM32_TIM_SR(TIM_RX) = 0;

	/* --- DAC configuration for comparator at 550mV --- */
	/* Enable DAC interface clock. */
	STM32_RCC_APB1ENR |= (1 << 29);
	/* set voltage Vout=0.550V (Vref = 3.0V) */
	STM32_DAC_DHR12RD = 550 * 4096 / 3000;
	/* Start DAC channel 1 */
	STM32_DAC_CR = STM32_DAC_CR_EN1 | STM32_DAC_CR_BOFF1;

	/* --- COMP2 as comparator for RX vs Vmid = 550mV --- */
	/* turn on COMP/SYSCFG */
	STM32_RCC_APB2ENR |= 1 << 0;
	/* currently in hi-speed mode : INP = PA1 , INM = DAC1 / PA4 / INM4 */
	STM32_COMP_CSR = STM32_COMP_CMP1EN | STM32_COMP_CMP1MODE_HSPEED |
			 STM32_COMP_CMP1INSEL_VREF12 |
			 /*STM32_COMP_CMP1INSEL_INM4 |*/
			 STM32_COMP_CMP1OUTSEL_TIM1_IC1 |
			 STM32_COMP_CMP1HYST_HI;
	ccprintf("Sniffer initialized\n");


	/* start sampling the edges on the CC line using the RX timer */
	dma_start_rx(&dma_tim_option, RX_COUNT, samples);
	task_enable_irq(STM32_IRQ_DMA_CHANNEL_4_7);
	/* start RX timer */
	STM32_TIM_CR1(TIM_RX) |= 1;
}
DECLARE_HOOK(HOOK_INIT, sniffer_init, HOOK_PRIO_DEFAULT);

/* Task to post-process the samples and copy them the USB endpoint buffer */
void sniffer_task(void)
{
	uint16_t cnt = 0;
	int off = 0;
	uint8_t freebuf = 3;
	struct stm32_endpoint *ep = btable_ep+USB_EP_SNIFFER;

	while (1) {
		/* Wait for a new buffer of samples or a new USB free buffer */
		uint32_t evt = task_wait_event(-1);

		freebuf |= evt & USB_EVENTS;
		if (evt & ~USB_EVENTS) {
			off = evt & STM32_DMA_ISR_TCIF(DMAC_TIM_RX) ?
				RX_COUNT/2 : 0;
			cnt += RX_COUNT/2;
			if (cnt > RX_COUNT) {
				cnt -= RX_COUNT/2;
				oflow++;
			}
		}
		/* send the available samples over USB if we have a buffer*/
		while (cnt && freebuf) {
			int b = freebuf & 1 ? 0 : 1;
			freebuf &= ~(1 << b);
			ep_buf[b][0] = (seq << 8) | 1 /* CC1 stream */;
			ep_buf[b][1] = __hw_clock_source_read();
			memcpy_usbram(ep_buf[b] + 2,
				      samples+off, EP_PAYLOAD_SIZE);
			if (b)
				ep->rx_count = EP_BUF_SIZE;
			else
				ep->tx_count = EP_BUF_SIZE;
			cnt -= EP_PAYLOAD_SIZE;
			off += EP_PAYLOAD_SIZE;
			if (off >= RX_COUNT)
				off = 0;
		}
	}
}

static int command_sniffer(int argc, char **argv)
{
	ccprintf("Seq number:%d Overflows: %d\n", seq, oflow);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sniffer, command_sniffer,
			"[]", "Buffering status", NULL);
