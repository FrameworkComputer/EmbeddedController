/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

#ifdef CONFIG_USB_BOS
/* v2.01 (vs 2.00) BOS Descriptor provided */
#define USB_DEV_BCDUSB 0x0201
#else
#define USB_DEV_BCDUSB 0x0200
#endif

#ifndef USB_DEV_CLASS
#define USB_DEV_CLASS USB_CLASS_PER_INTERFACE
#endif

/* USB Standard Device Descriptor */
static const struct usb_device_descriptor dev_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = USB_DEV_BCDUSB,
	.bDeviceClass = USB_DEV_CLASS,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = USB_MAX_PACKET_SIZE,
	.idVendor = USB_VID_GOOGLE,
	.idProduct = CONFIG_USB_PID,
	.bcdDevice = 0x0200, /* 2.00 */
	.iManufacturer = USB_STR_VENDOR,
	.iProduct = USB_STR_PRODUCT,
	.iSerialNumber = 0,
	.bNumConfigurations = 1
};

/* USB Configuration Descriptor */
const struct usb_config_descriptor USB_CONF_DESC(conf) = {
	.bLength = USB_DT_CONFIG_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0x0BAD, /* no of returned bytes, set at runtime */
	.bNumInterfaces = USB_IFACE_COUNT,
	.bConfigurationValue = 1,
	.iConfiguration = USB_STR_VERSION,
	.bmAttributes = 0x80, /* bus powered */
	.bMaxPower = 250, /* MaxPower 500 mA */
};

const uint8_t usb_string_desc[] = {
	4, /* Descriptor size */
	USB_DT_STRING,
	0x09, 0x04 /* LangID = 0x0409: U.S. English */
};

/* Endpoint table in USB controller RAM */
struct stm32_endpoint btable_ep[USB_EP_COUNT]
	__attribute__((section(".usb_ram.btable")));
/* Control endpoint (EP0) buffers */
static usb_uint ep0_buf_tx[USB_MAX_PACKET_SIZE / 2] __usb_ram;
static usb_uint ep0_buf_rx[USB_MAX_PACKET_SIZE / 2] __usb_ram;

static int set_addr;
/* remaining size of descriptor data to transfer */
static int desc_left;
/* pointer to descriptor data if any */
static const uint8_t *desc_ptr;

/* Requests on the control endpoint (aka EP0) */
static void ep0_rx(void)
{
	uint16_t req = ep0_buf_rx[0]; /* bRequestType | bRequest */

	/* reset any incomplete descriptor transfer */
	desc_ptr = NULL;

	/* interface specific requests */
	if ((req & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		uint8_t iface = ep0_buf_rx[2] & 0xff;
		if (iface < USB_IFACE_COUNT)
			usb_iface_request[iface](ep0_buf_rx, ep0_buf_tx);
		return;
	}

	/* TODO check setup bit ? */
	if (req == (USB_DIR_IN | (USB_REQ_GET_DESCRIPTOR << 8))) {
		uint8_t type = ep0_buf_rx[1] >> 8;
		uint8_t idx = ep0_buf_rx[1] & 0xff;
		const uint8_t *desc;
		int len;

		switch (type) {
		case USB_DT_DEVICE: /* Setup : Get device descriptor */
			desc = (void *)&dev_desc;
			len = sizeof(dev_desc);
			break;
		case USB_DT_CONFIGURATION: /* Setup : Get configuration desc */
			desc = __usb_desc;
			len = USB_DESC_SIZE;
			break;
#ifdef CONFIG_USB_BOS
		case USB_DT_BOS: /* Setup : Get BOS descriptor */
			desc = bos_ctx.descp;
			len = bos_ctx.size;
			break;
#endif
		case USB_DT_STRING: /* Setup : Get string descriptor */
			if (idx >= USB_STR_COUNT)
				/* The string does not exist : STALL */
				goto unknown_req;
			if (idx == USB_STR_VERSION) {
				/* use the generated firmware version string */
				desc = usb_fw_version;
				len = desc[0];
				break;
			}
			desc = usb_strings[idx];
			len = desc[0];
			break;
		case USB_DT_DEVICE_QUALIFIER: /* Get device qualifier desc */
			/* Not high speed : STALL next IN used as handshake */
			goto unknown_req;
		default: /* unhandled descriptor */
			goto unknown_req;
		}
		/* do not send more than what the host asked for */
		len = MIN(ep0_buf_rx[3], len);
		/*
		 * if we cannot transmit everything at once,
		 * keep the remainder for the next IN packet
		 */
		if (len >= USB_MAX_PACKET_SIZE) {
			desc_left = len - USB_MAX_PACKET_SIZE;
			desc_ptr = desc + USB_MAX_PACKET_SIZE;
			len = USB_MAX_PACKET_SIZE;
		}
		memcpy_usbram(ep0_buf_tx, desc, len);
		if (type == USB_DT_CONFIGURATION)
			/* set the real descriptor size */
			ep0_buf_tx[1] = USB_DESC_SIZE;
		btable_ep[0].tx_count = len;
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
				desc_left ? 0 : EP_STATUS_OUT);
		/* send the null OUT transaction if the transfer is complete */
	} else if (req == (USB_DIR_IN | (USB_REQ_GET_STATUS << 8))) {
		uint16_t zero = 0;
		/* Get status */
		memcpy_usbram(ep0_buf_tx, (void *)&zero, 2);
		btable_ep[0].tx_count = 2;
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
			  EP_STATUS_OUT /*null OUT transaction */);
	} else if ((req & 0xff) == USB_DIR_OUT) {
		switch (req >> 8) {
		case USB_REQ_SET_ADDRESS:
			/* set the address after we got IN packet handshake */
			set_addr = ep0_buf_rx[1] & 0xff;
			/* need null IN transaction -> TX Valid */
			btable_ep[0].tx_count = 0;
			STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
			break;
		case USB_REQ_SET_CONFIGURATION:
			/* uint8_t cfg = ep0_buf_rx[1] & 0xff; */
			/* null IN for handshake */
			btable_ep[0].tx_count = 0;
			STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
			break;
		default: /* unhandled request */
			goto unknown_req;
		}

	} else {
		goto unknown_req;
	}

	return;
unknown_req:
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_RX_VALID | EP_TX_STALL, 0);
}

static void ep0_tx(void)
{
	if (set_addr) {
		STM32_USB_DADDR = set_addr | 0x80;
		set_addr = 0;
		CPRINTF("SETAD %02x\n", STM32_USB_DADDR);
	}
	if (desc_ptr) {
		/* we have an on-going descriptor transfer */
		int len = MIN(desc_left, USB_MAX_PACKET_SIZE);
		memcpy_usbram(ep0_buf_tx, desc_ptr, len);
		btable_ep[0].tx_count = len;
		desc_left -= len;
		desc_ptr += len;
		STM32_TOGGLE_EP(0, EP_TX_MASK, EP_TX_VALID,
				desc_left ? 0 : EP_STATUS_OUT);
		/* send the null OUT transaction if the transfer is complete */
		return;
	}
	STM32_TOGGLE_EP(0, EP_TX_MASK, EP_TX_VALID, 0);
}

static void ep0_reset(void)
{
	STM32_USB_EP(0) = (1 << 9) /* control EP */ |
			  (2 << 4) /* TX NAK */ |
			  (3 << 12) /* RX VALID */;

	btable_ep[0].tx_addr = usb_sram_addr(ep0_buf_tx);
	btable_ep[0].rx_addr = usb_sram_addr(ep0_buf_rx);
	btable_ep[0].rx_count = 0x8000 | ((USB_MAX_PACKET_SIZE/32-1) << 10);
	btable_ep[0].tx_count = 0;
}
USB_DECLARE_EP(0, ep0_tx, ep0_rx, ep0_reset);

static void usb_reset(void)
{
	int ep;

	for (ep = 0; ep < USB_EP_COUNT; ep++)
		usb_ep_reset[ep]();

	/*
	 * set the default address : 0
	 * as we are not configured yet
	 */
	STM32_USB_DADDR = 0 | 0x80;
	CPRINTF("RST EP0 %04x\n", STM32_USB_EP(0));
}

void usb_interrupt(void)
{
	uint16_t status = STM32_USB_ISTR;

	if ((status & (1 << 10)))
		usb_reset();

	if (status & (1 << 15)) {
		int ep = status & 0x000f;
		if (ep < USB_EP_COUNT) {
			if (status & 0x0010)
				usb_ep_rx[ep]();
			else
				usb_ep_tx[ep]();
		}
		/* TODO: do it in a USB task */
		/* task_set_event(, 1 << ep_task); */
	}

	/* ack interrupts */
	STM32_USB_ISTR = 0;
}
DECLARE_IRQ(STM32_IRQ_USB_LP, usb_interrupt, 1);

void usb_init(void)
{
	/* Enable USB device clock. */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_USB;

	/* we need a proper 48MHz clock */
	clock_enable_module(MODULE_USB, 1);

	/* configure the pinmux */
	gpio_config_module(MODULE_USB, 1);

	/* power on sequence */

	/* keep FRES (USB reset) and remove PDWN (power down) */
	STM32_USB_CNTR = 0x01;
	udelay(1); /* startup time */
	/* reset FRES and keep interrupts masked */
	STM32_USB_CNTR = 0x00;
	/* clear pending interrupts */
	STM32_USB_ISTR = 0;

	/* set descriptors table offset in dedicated SRAM */
	STM32_USB_BTABLE = 0;

	/* EXTI18 is USB wake up interrupt */
	/* STM32_EXTI_RTSR |= 1 << 18; */
	/* STM32_EXTI_IMR |= 1 << 18; */

	/* Enable interrupt handlers */
	task_enable_irq(STM32_IRQ_USB_LP);
	/* set interrupts mask : reset/correct tranfer/errors */
	STM32_USB_CNTR = 0xe400;

#ifndef CONFIG_USB_INHIBIT
	usb_connect();
#endif

	CPRINTF("USB init done\n");
}
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);

void usb_disconnect(void)
{
	/* disable pull-up on DP to disconnect */
#ifdef CHIP_VARIANT_STM32L15X
	STM32_SYSCFG_PMC &= ~1;
#elif defined(CHIP_FAMILY_STM32F0)
	STM32_USB_BCDR &= ~(1 << 15) /* DPPU */;
#else
#warn "usb disconnect not implemented for this chip family"
#endif
}

void usb_connect(void)
{
	/* enable pull-up on DP to connect */
#ifdef CHIP_VARIANT_STM32L15X
	STM32_SYSCFG_PMC |= 1;
#elif defined(CHIP_FAMILY_STM32F0)
	STM32_USB_BCDR |= (1 << 15) /* DPPU */;
#else
#warn "usb connect not implemented for this chip family"
#endif
}

void usb_release(void)
{
	/* signal disconnect to host */
#ifdef CHIP_VARIANT_STM32L15X
	STM32_SYSCFG_PMC &= ~1;
#elif defined(CHIP_FAMILY_STM32F0)
	STM32_USB_BCDR &= ~(1 << 15) /* DPPU */;
#else
	/* hardwired or regular GPIO on other platforms */
#endif

	/* power down USB */
	STM32_USB_CNTR = 0;

	/* disable interrupt handlers */
	task_disable_irq(STM32_IRQ_USB_LP);

	/* unset pinmux */
	gpio_config_module(MODULE_USB, 0);

	/* disable 48MHz clock */
	clock_enable_module(MODULE_USB, 0);

	/* disable USB device clock */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_USB;
}
