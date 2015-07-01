/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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

/* Rev A1 has a RTL bug in the FIFO */
#if CONCAT2(GC_, GC___MAJOR_REV__) == GC___REVA__
/*
 * WORKAROUND: only the first 256 entries are usable as TX FIFO
 *
 * Use the last 128 entries for EP_INFO (not affected by the bug)
 * and 256 entries for RX/TX FIFOs : total 384 entries.
 *
 * RX FIFO needs more than 64 entries (for reserved space)
 * set RX FIFO to 80 entries
 * set TX0-TX10 FIFO to 64 bytes = 16 (x 32-bit) entries
 * let TX11-TX15 uninitialized for now (WORKAROUND).
 */
#define FIFO_SIZE   0x180
#define TX_FIFO_CNT 11
#else
#define FIFO_SIZE   0x400
#define TX_FIFO_CNT 16
#endif

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
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

#ifndef CONFIG_USB_BCD_DEV
#define CONFIG_USB_BCD_DEV 0x0100 /* 1.00 */
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
	.bcdDevice = CONFIG_USB_BCD_DEV,
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

/* Descriptors for USB controller S/G DMA */
struct g_usb_desc ep0_out_desc;
struct g_usb_desc ep0_in_desc;

/* Control endpoint (EP0) buffers */
static uint8_t ep0_buf_tx[USB_MAX_PACKET_SIZE];
static uint8_t ep0_buf_rx[USB_MAX_PACKET_SIZE];

static int set_addr;
/* remaining size of descriptor data to transfer */
static int desc_left;
/* pointer to descriptor data if any */
static const uint8_t *desc_ptr;

/*
 * Requests on the control endpoint (aka EP0). The USB spec mandates that all
 * values are little-endian over the wire. Since this file is intentionally
 * chip-specific and we're a little-endian architecture, we can just cast the
 * buffer into the correct struct.
 */
static void ep0_rx(void)
{
	uint32_t epint = GR_USB_DOEPINT(0);
	struct usb_setup_packet *req = (struct usb_setup_packet *)ep0_buf_rx;

	GR_USB_DOEPINT(0) = epint; /* clear IT */

	/* reset any incomplete descriptor transfer */
	desc_ptr = NULL;

	/* interface specific requests */
	if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		uint8_t iface = req->wIndex & 0xff;
		if (iface < USB_IFACE_COUNT &&
		    usb_iface_request[iface](ep0_buf_rx, ep0_buf_tx))
			goto unknown_req;
		return;
	}

	if (req->bmRequestType == USB_DIR_IN &&
	    req->bRequest == USB_REQ_GET_DESCRIPTOR) {
		uint8_t type = req->wValue >> 8;
		uint8_t idx = req->wValue & 0xff;
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
		len = MIN(req->wLength, len);
		/*
		 * if we cannot transmit everything at once,
		 * keep the remainder for the next IN packet
		 */
		if (len >= USB_MAX_PACKET_SIZE) {
			desc_left = len - USB_MAX_PACKET_SIZE;
			desc_ptr = desc + USB_MAX_PACKET_SIZE;
			len = USB_MAX_PACKET_SIZE;
		}
		memcpy(ep0_buf_tx, desc, len);
		if (type == USB_DT_CONFIGURATION) {
			struct usb_config_descriptor *cfg =
				(struct usb_config_descriptor *)ep0_buf_tx;
			/* set the real descriptor size */
			cfg->wTotalLength = USB_DESC_SIZE;
		}
		ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY |
				    DIEPDMA_IOC | DIEPDMA_TXBYTES(len);
		GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
		ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST
				   | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
		GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
		/* send the null OUT transaction if the transfer is complete */
	} else if (req->bmRequestType == USB_DIR_IN &&
		   req->bRequest == USB_REQ_GET_STATUS) {
		uint16_t zero = 0;
		/* Get status */
		memcpy(ep0_buf_tx, &zero, 2);
		ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC |
				    DIEPDMA_TXBYTES(2);
		GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
		ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST
				   | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
		GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
	} else if (req->bmRequestType == USB_DIR_OUT) {
		switch (req->bRequest) {
		case USB_REQ_SET_ADDRESS:
			/* set the address after we got IN packet handshake */
			set_addr = req->wValue & 0xff;
			/* need null IN transaction -> TX Valid */
			ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC |
					    DIEPDMA_TXBYTES(0) | DIEPDMA_SP;
			GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
			ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST
					   | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
			GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
			break;
		case USB_REQ_SET_CONFIGURATION:
			/* uint8_t cfg = req->wValue & 0xff; */
			/* null IN for handshake */
			ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC |
					    DIEPDMA_TXBYTES(0) | DIEPDMA_SP;
			GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
			ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST
					   | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
			GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
			break;
		default: /* unhandled request */
			goto unknown_req;
		}

	} else {
		goto unknown_req;
	}

	return;
unknown_req:
	ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST |
			     DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
	GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
	GR_USB_DIEPCTL(0) |= DXEPCTL_STALL | DXEPCTL_EPENA;
	return;
}

static void ep0_tx(void)
{
	uint32_t epint = GR_USB_DIEPINT(0);

	GR_USB_DIEPINT(0) = epint; /* clear IT */

	if (set_addr) {
		GR_USB_DCFG = (GR_USB_DCFG & ~DCFG_DEVADDR(0x7f))
			    | DCFG_DEVADDR(set_addr);
		CPRINTS("SETAD 0x%02x (%d)", set_addr, set_addr);
		set_addr = 0;
	}
	if (desc_ptr) {
		/* we have an on-going descriptor transfer */
		int len = MIN(desc_left, USB_MAX_PACKET_SIZE);
		memcpy(ep0_buf_tx, desc_ptr, len);
		ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY |
				    DIEPDMA_IOC | DIEPDMA_TXBYTES(len);
		desc_left -= len;
		desc_ptr += len;
		/* send the null OUT transaction if the transfer is complete */
		GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
		/* TODO set Data PID in DIEPCTL */
		return;
	}
}

static void ep0_reset(void)
{
	ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST |
			     DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
	ep0_out_desc.addr = ep0_buf_rx;
	ep0_in_desc.flags = DIEPDMA_TXBYTES(0) | DIEPDMA_LAST |
			    DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC;
	ep0_in_desc.addr = ep0_buf_tx;
	GR_USB_DIEPDMA(0) = (uint32_t)&ep0_in_desc;
	GR_USB_DOEPDMA(0) = (uint32_t)&ep0_out_desc;
	GR_USB_DOEPCTL(0) = DXEPCTL_MPS64 | DXEPCTL_USBACTEP |
			    DXEPCTL_EPTYPE_CTRL |
			    DXEPCTL_CNAK | DXEPCTL_EPENA;
	GR_USB_DIEPCTL(0) = DXEPCTL_MPS64 | DXEPCTL_USBACTEP |
			    DXEPCTL_EPTYPE_CTRL;
	GR_USB_DAINTMSK = (1<<0) | (1 << (0+16)); /* EPO interrupts */
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
	GR_USB_DCFG &= ~DCFG_DEVADDR(0x7f);
}

void usb_interrupt(void)
{
	uint32_t status = GR_USB_GINTSTS;

	if (status & GINTSTS_USBRST)
		usb_reset();

	if (status & (GINTSTS_OEPINT | GINTSTS_IEPINT)) {
		uint32_t daint = GR_USB_DAINT;
		int ep;
		for (ep = 0; ep < USB_EP_COUNT && daint; ep++, daint >>= 1) {
			if (daint & (1 << 16)) /* OUT packet */
				usb_ep_rx[ep]();
			if (daint & 1) /* IN packet */
				usb_ep_tx[ep]();
		}
	}

	if (status & GINTSTS_GOUTNAKEFF)
		GR_USB_DCTL = DCTL_CGOUTNAK;

	if (status & GINTSTS_GINNAKEFF)
		GR_USB_DCTL = DCTL_CGNPINNAK;

	/* ack interrupts */
	GR_USB_GINTSTS = status;
}
DECLARE_IRQ(GC_IRQNUM_USB0_USBINTR, usb_interrupt, 1);

static void usb_softreset(void)
{
	int timeout;

	GR_USB_GRSTCTL = GRSTCTL_CSFTRST;
	timeout = 10000;
	while ((GR_USB_GRSTCTL & GRSTCTL_CSFTRST) && timeout-- > 0)
		;
	if (GR_USB_GRSTCTL & GRSTCTL_CSFTRST) {
		CPRINTF("USB: reset failed\n");
		return;
	}

	timeout = 10000;
	while (!(GR_USB_GRSTCTL & GRSTCTL_AHBIDLE) && timeout-- > 0)
		;
	if (!timeout) {
		CPRINTF("USB: reset timeout\n");
		return;
	}
}

void usb_connect(void)
{
	GR_USB_DCTL &= ~DCTL_SFTDISCON;
}

void usb_disconnect(void)
{
	GR_USB_DCTL |= DCTL_SFTDISCON;
}

void usb_init(void)
{
	int i;
	/* Enable clocks */
	clock_enable_module(MODULE_USB, 1);

	/* set up pinmux */
	gpio_config_module(MODULE_USB, 1);

	/* Use the last 128 entries of the FIFO for EP INFO */
	GR_USB_GDFIFOCFG = ((FIFO_SIZE - 0x80) << 16) | FIFO_SIZE;

	/* PHY configuration */
	/* Full-Speed Serial PHY */
	GR_USB_GUSBCFG = GUSBCFG_PHYSEL_FS | GUSBCFG_FSINTF_6PIN
			| GUSBCFG_TOUTCAL(7) | (9 << 10);
	usb_softreset();

	/* PHY configuration */
	/* Full-Speed Serial PHY */
	GR_USB_GUSBCFG = GUSBCFG_PHYSEL_FS | GUSBCFG_FSINTF_6PIN
			| GUSBCFG_TOUTCAL(7) | (9 << 10);
	/* Global + DMA configuration */
	GR_USB_GAHBCFG = GAHBCFG_DMA_EN | GAHBCFG_GLB_INTR_EN |
			 GAHBCFG_NP_TXF_EMP_LVL;

	/* unmask subset of endpoint interrupts */
	GR_USB_DIEPMSK = DIEPMSK_TIMEOUTMSK | DIEPMSK_AHBERRMSK |
			 DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK |
			 DIEPMSK_INTKNEPMISMSK;
	GR_USB_DOEPMSK = DOEPMSK_SETUPMSK | DOEPMSK_AHBERRMSK |
			 DOEPMSK_EPDISBLDMSK | DOEPMSK_XFERCOMPLMSK;
	GR_USB_DAINTMSK = 0;

	/* Be in disconnected state we are ready */
	GR_USB_DCTL |= DCTL_SFTDISCON;

	/* Max speed: USB2 FS */
	GR_USB_DCFG = DCFG_DEVSPD_FS48 | DCFG_DESCDMA;

	/* clear pending interrupts */
	GR_USB_GINTSTS = 0xFFFFFFFF;

	/*
	 * Setup FIFOs configuration
	 * RX FIFO needs more than 64 entries (for reserved space)
	 * set RX FIFO to 80 entries
	 * set TX FIFO to 64 bytes = 16 (x 32-bit) entries
	 */
	GR_USB_GRXFSIZ = 80;
	GR_USB_GNPTXFSIZ = 80 | (16 << 16);
	for (i = 1; i < TX_FIFO_CNT; i++)
		GR_USB_DIEPTXF(i) = (80 + i*16) | (16 << 16);
	/* Flush all FIFOs */
	GR_USB_GRSTCTL = GRSTCTL_TXFNUM(0x10) | GRSTCTL_TXFFLSH
					      | GRSTCTL_RXFFLSH;
	while (GR_USB_GRSTCTL & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH))
		; /* timeout 100ms */

	/* Initialize endpoints */
	for (i = 0; i < 16; i++) {
		GR_USB_DIEPCTL(i) = 0x00/* TODO  */;
		GR_USB_DOEPCTL(i) = 0x00/* TODO */;
	}

	/* Device registers have been setup */
	GR_USB_DCTL |= DCTL_PWRONPRGDONE;
	udelay(10);
	GR_USB_DCTL &= ~DCTL_PWRONPRGDONE;

	/* Clear global NAKs */
	GR_USB_DCTL |= DCTL_CGOUTNAK | DCTL_CGNPINNAK;

	/* Enable interrupt handlers */
	task_enable_irq(GC_IRQNUM_USB0_USBINTR);
	/* set interrupts mask : reset/correct tranfer/errors */
	GR_USB_GINTMSK = GINTSTS_GOUTNAKEFF | GINTSTS_GINNAKEFF |
			 GINTSTS_USBRST | GINTSTS_ENUMDONE |
			 GINTSTS_OEPINT | GINTSTS_IEPINT;

#ifndef CONFIG_USB_INHIBIT_CONNECT
	usb_connect();
#endif

	CPRINTS("USB init done");
}
#ifndef CONFIG_USB_INHIBIT_INIT
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);
#endif

void usb_release(void)
{
	/* signal disconnect to host */
	usb_disconnect();

	/* disable interrupt handlers */
	task_disable_irq(GC_IRQNUM_USB0_USBINTR);

	/* disable clocks */
	clock_enable_module(MODULE_USB, 0);
	/* TODO: pin-mux */
}
