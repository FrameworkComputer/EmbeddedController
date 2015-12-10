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
#include "usb_descriptor.h"
#include "watchdog.h"

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

/* This is not defined anywhere else. Change it here to debug. */
#undef DEBUG_ME
#ifdef DEBUG_ME
/*
 * For debugging we want to print a bunch of things from within the interrupt
 * handlers, but if we try it'll 1) stop working, and 2) mess up the timing
 * that we're trying to measure. Instead we fill a circular buffer with things
 * to print when we get the chance. The number of args is fixed (a format
 * string and five uint32_t args), and will be printed a few at a time in a
 * HOOK_TICK handler.
 *
 */
#define MAX_ENTRIES 350				/* Chosen arbitrarily */
static struct {
	timestamp_t t;
	const char *fmt;
	int a0, a1, a2, a3, a4;
} stuff_to_print[MAX_ENTRIES];
static int stuff_in, stuff_out, stuff_overflow;

/* Call this only from within interrupt handler! */
void print_later(const char *fmt, int a0, int a1, int a2, int a3, int a4)
{
	int next;

	stuff_to_print[stuff_in].t = get_time();
	stuff_to_print[stuff_in].fmt = fmt;
	stuff_to_print[stuff_in].a0 = a0;
	stuff_to_print[stuff_in].a1 = a1;
	stuff_to_print[stuff_in].a2 = a2;
	stuff_to_print[stuff_in].a3 = a3;
	stuff_to_print[stuff_in].a4 = a4;

	next = (stuff_in + 1) % MAX_ENTRIES;
	if (next == stuff_out)
		stuff_overflow++;
	else
		stuff_in = next;
}

static void do_print_later(void)
{
	int lines_per_loop = 32;		/* too much at once fails */
	int copy_of_stuff_in;
	int copy_of_overflow;

	interrupt_disable();
	copy_of_stuff_in = stuff_in;
	copy_of_overflow = stuff_overflow;
	stuff_overflow = 0;
	interrupt_enable();

	if (copy_of_overflow)
		ccprintf("*** WARNING: %d MESSAGES WERE LOST ***\n",
			 copy_of_overflow);

	while (lines_per_loop && stuff_out != copy_of_stuff_in) {
		ccprintf("at %.6ld: ", stuff_to_print[stuff_out].t);
		ccprintf(stuff_to_print[stuff_out].fmt,
			 stuff_to_print[stuff_out].a0,
			 stuff_to_print[stuff_out].a1,
			 stuff_to_print[stuff_out].a2,
			 stuff_to_print[stuff_out].a3,
			 stuff_to_print[stuff_out].a4);
		ccprintf("\n");
		stuff_out = (stuff_out + 1) % MAX_ENTRIES;
		lines_per_loop--;
	}
}
DECLARE_HOOK(HOOK_TICK, do_print_later, HOOK_PRIO_DEFAULT);

/* Debugging stuff to display some registers and bits */
static const char const *deezbits[32] = {
	[0]     = "CURMOD",
	[1]     = "MODEMIS",
	[2]     = "OTGINT",
	[3]     = "SOF",
	[4]     = "RXFLVL",
	[6]     = "GINNAKEFF",
	[7]     = "GOUTNAKEFF",
	[10]    = "ERLYSUSP",
	[11]    = "USBSUSP",
	[12]    = "USBRST",
	[13]    = "ENUMDONE",
	[14]    = "ISOOUTDROP",
	[15]    = "EOPF",
	[17]    = "EPMIS",
	[18]    = "IEPINT",
	[19]    = "OEPINT",
	[20]    = "INCOMPISOIN",
	[21]    = "INCOMPLP",
	[22]    = "FETSUSP",
	[23]    = "RESETDET",
	[28]    = "CONIDSTSCHNG",
	[30]    = "SESSREQINT",
	[31]    = "WKUPINT",
};

static void showbits(uint32_t b)
{
	int i;

	for (i = 0; i < 32; i++)
		if (b & (1 << i)) {
			if (deezbits[i])
				ccprintf(" %s", deezbits[i]);
			else
				ccprintf(" %d", i);
		}
	ccprintf("\n");
}

static int command_usb(int argc, char **argv)
{
	ccprintf("GINTSTS:   0x%08x\n", GR_USB_GINTSTS);
	showbits(GR_USB_GINTSTS);
	ccprintf("GINTMSK:   0x%08x\n", GR_USB_GINTMSK);
	showbits(GR_USB_GINTMSK);
	ccprintf("DAINT:     0x%08x\n", GR_USB_DAINT);
	ccprintf("DAINTMSK:  0x%08x\n", GR_USB_DAINTMSK);
	ccprintf("DOEPMSK:   0x%08x\n", GR_USB_DOEPMSK);
	ccprintf("DIEPMSK:   0x%08x\n", GR_USB_DIEPMSK);
	ccprintf("DCFG:      0x%08x\n", GR_USB_DCFG);
	ccprintf("DOEPCTL0:  0x%08x\n", GR_USB_DOEPCTL(0));
	ccprintf("DIEPCTL0:  0x%08x\n", GR_USB_DIEPCTL(0));
	ccprintf("DOEPCTL1:  0x%08x\n", GR_USB_DOEPCTL(1));
	ccprintf("DIEPCTL1:  0x%08x\n", GR_USB_DIEPCTL(1));
	ccprintf("DOEPCTL2:  0x%08x\n", GR_USB_DOEPCTL(2));
	ccprintf("DIEPCTL2:  0x%08x\n", GR_USB_DIEPCTL(2));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(usb, command_usb,
			"",
			"Show some USB regs",
			NULL);

/* When debugging, print errors as they occur */
#define report_error(dummy)					\
		print_later("USB ERROR at usb.c line %d",	\
			    __LINE__, 0, 0, 0, 0)

#else  /* Not debugging */
#define print_later(...)

/* TODO: Something unexpected happened. Figure out how to report & fix it. */
#define report_error(dummy)						\
		CPRINTS("Unhandled USB error at %s line %d",		\
			__FILE__, __LINE__)

#endif	/* DEBUG_ME */

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
#define CONFIG_USB_BCD_DEV 0x0100		/* 1.00 */
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
	.wTotalLength = 0x0BAD,	 /* number of returned bytes, set at runtime */
	.bNumInterfaces = USB_IFACE_COUNT,
	.bConfigurationValue = 1,
	.iConfiguration = USB_STR_VERSION,
	.bmAttributes = 0x80,			/* bus powered */
	.bMaxPower = 250,			/* MaxPower 500 mA */
};

const uint8_t usb_string_desc[] = {
	4,					/* Descriptor size */
	USB_DT_STRING,
	0x09, 0x04			    /* LangID = 0x0409: U.S. English */
};

/* Descriptors for USB controller S/G DMA */
static struct g_usb_desc ep0_out_desc;
static struct g_usb_desc ep0_in_desc;

/* Control endpoint (EP0) buffers */
static uint8_t ep0_buf_tx[USB_MAX_PACKET_SIZE];
static uint8_t ep0_buf_rx[USB_MAX_PACKET_SIZE];

static int set_addr;
/* remaining size of descriptor data to transfer */
static int desc_left;
/* pointer to descriptor data if any */
static const uint8_t *desc_ptr;

/* Load the EP0 IN FIFO buffer with some data (zero-length works too). Returns
 * len, or negative on error. */
int load_in_fifo(const void *source, uint32_t len)
{
	if (len > sizeof(ep0_buf_tx))
		return -1;

	memcpy(ep0_buf_tx, source, len);
	ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY |
		DIEPDMA_IOC | DIEPDMA_TXBYTES(len);
	GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;

	return len;
}

/* Prepare the EP0 OUT FIFO buffer to accept some data. Returns len, or
 * negative on error. */
int accept_out_fifo(uint32_t len)
{
	/* TODO: This is not yet implemented */
	return -1;
}

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

	print_later("ep0_rx: DOEPINT(0) is 0x%x", epint, 0, 0, 0, 0);

	GR_USB_DOEPINT(0) = epint; /* clear IT */

	print_later("R: %02x %02x %04x %04x %04x",
		    req->bmRequestType, req->bRequest, req->wValue,
		    req->wIndex, req->wLength);

	/* reset any incomplete descriptor transfer */
	desc_ptr = NULL;

	/* interface specific requests */
	if ((req->bmRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		uint8_t iface = req->wIndex & 0xff;
		int bytes;

		if (iface >= USB_IFACE_COUNT)
			goto unknown_req;

		bytes = usb_iface_request[iface](req);
		if (bytes < 0)
			goto unknown_req;

		ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST
			| DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
		GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
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
			/*
			 * Set the address after the IN packet handshake.
			 *
			 * From the USB 2.0 spec, section 9.4.6:
			 *
			 * As noted elsewhere, requests actually may result in
			 * up to three stages. In the first stage, the Setup
			 * packet is sent to the device. In the optional second
			 * stage, data is transferred between the host and the
			 * device. In the final stage, status is transferred
			 * between the host and the device. The direction of
			 * data and status transfer depends on whether the host
			 * is sending data to the device or the device is
			 * sending data to the host. The Status stage transfer
			 * is always in the opposite direction of the Data
			 * stage. If there is no Data stage, the Status stage
			 * is from the device to the host.
			 *
			 * Stages after the initial Setup packet assume the
			 * same device address as the Setup packet. The USB
			 * device does not change its device address until
			 * after the Status stage of this request is completed
			 * successfully. Note that this is a difference between
			 * this request and all other requests. For all other
			 * requests, the operation indicated must be completed
			 * before the Status stage
			 */
			set_addr = req->wValue & 0xff;
			/*
			 * NOTE: Now that we've said that, we don't do it. The
			 * hardware for this SoC knows that an IN packet will
			 * be following the SET ADDRESS, so it waits until it
			 * sees that happen before the address change takes
			 * effect. If we wait until after the IN packet to
			 * change the register, the hardware gets confused and
			 * doesn't respond to anything.
			 */
			GWRITE_FIELD(USB, DCFG, DEVADDR, set_addr);
			print_later("SETAD 0x%02x (%d)",
				    set_addr, set_addr, 0, 0, 0);
			/* still need a null IN transaction -> TX Valid */
			ep0_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY
				| DIEPDMA_IOC | DIEPDMA_TXBYTES(0) | DIEPDMA_SP;
			GR_USB_DIEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
			ep0_out_desc.flags = DOEPDMA_RXBYTES(64) | DOEPDMA_LAST
					   | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
			GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
			break;
		case USB_REQ_SET_CONFIGURATION:
			/* uint8_t cfg = req->wValue & 0xff; */
			print_later("SETCFG 0x%x", req->wValue, 0, 0, 0, 0);
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
	print_later("unknown req", 0, 0, 0, 0, 0);
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

	/* This is where most SoCs would change the address.
	 * We don't. See the note above. */
	if (set_addr) {
		print_later("STATUS SETAD 0x%02x (%d)",
			    set_addr, set_addr, 0, 0, 0);
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
	/* Reset EP0 address */
	GWRITE_FIELD(USB, DCFG, DEVADDR, 0);

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
	GR_USB_DAINTMSK = DAINT_OUTEP(0) | DAINT_INEP(0);
}
USB_DECLARE_EP(0, ep0_tx, ep0_rx, ep0_reset);

static void setup_data_fifos(void)
{
	int i;

	print_later("setup_data_fifos()", 0, 0, 0, 0, 0);

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
}

static void usb_reset(void)
{
	int ep;

	print_later("usb_reset()", 0, 0, 0, 0, 0);

	for (ep = 0; ep < USB_EP_COUNT; ep++)
		usb_ep_reset[ep]();
}

static void usb_enumdone(void)
{
	print_later("usb_enumdone()", 0, 0, 0, 0, 0);
}

static void usb_wakeup(void)
{
	print_later("usb_wakeup()", 0, 0, 0, 0, 0);
}

static void usb_early_suspend(void)
{
	print_later("usb_early_suspend()", 0, 0, 0, 0, 0);
}
static void usb_suspend(void)
{
	print_later("usb_suspend()", 0, 0, 0, 0, 0);
}

void usb_interrupt(void)
{
	uint32_t status = GR_USB_GINTSTS;
	uint32_t oepint = status & GINTSTS(OEPINT);
	uint32_t iepint = status & GINTSTS(IEPINT);

	int ep;

	print_later("interrupt: GINTSTS 0x%08x", status, 0, 0, 0, 0);

	if (status & GINTSTS(RESETDET))
		usb_wakeup();

	if (status & GINTSTS(ERLYSUSP))
		usb_early_suspend();

	if (status & GINTSTS(USBSUSP))
		usb_suspend();

	if (status & GINTSTS(USBRST))
		usb_reset();

	if (status & GINTSTS(ENUMDONE))
		usb_enumdone();

	/* Endpoint interrupts */
	if (oepint || iepint) {
		/* Note: It seems that the DAINT bits are only trustworthy for
		 * identifying interrupts when selected by the corresponding
		 * OEPINT and IEPINT bits from GINTSTS. */
		uint32_t daint = GR_USB_DAINT;

		for (ep = 0; ep < USB_EP_COUNT; ep++) {
			if (oepint && (daint & DAINT_OUTEP(ep)))
				usb_ep_rx[ep]();
			if (iepint && (daint & DAINT_INEP(ep)))
				usb_ep_tx[ep]();
		}
	}

	if (status & GINTSTS(GOUTNAKEFF))
		GR_USB_DCTL = DCTL_CGOUTNAK;

	if (status & GINTSTS(GINNAKEFF))
		GR_USB_DCTL = DCTL_CGNPINNAK;

	GR_USB_GINTSTS = status;

	print_later("end of interrupt", 0, 0, 0, 0, 0);
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
	/* TODO: Wait 3 PHY clocks before returning */
}

void usb_connect(void)
{
	print_later("usb_connect()", 0, 0, 0, 0, 0);
	GR_USB_DCTL &= ~DCTL_SFTDISCON;
}

void usb_disconnect(void)
{
	print_later("usb_disconnect()", 0, 0, 0, 0, 0);
	GR_USB_DCTL |= DCTL_SFTDISCON;
}

void usb_init(void)
{
	int i;

	/* TODO: Take this out if USB is known to always be present */
	if (!(GREG32(SWDP, FPGA_CONFIG) &
	      GC_CONST_SWDP_FPGA_CONFIG_USB_8X8CRYPTO)) {
		CPRINTF("This FPGA image has no USB support\n");
		return;
	}

	print_later("usb_init()", 0, 0, 0, 0, 0);

	/* TODO(crosbug.com/p/46813): Clean this up. Do only what's needed, and
	 * use meaningful constants instead of magic numbers. */
	GREG32(GLOBALSEC, DDMA0_REGION0_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DDMA0_REGION1_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DDMA0_REGION2_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DDMA0_REGION3_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION0_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION1_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION2_CTRL) = 0xffffffff;
	GREG32(GLOBALSEC, DUSB0_REGION3_CTRL) = 0xffffffff;

	/* Enable clocks */
	clock_enable_module(MODULE_USB, 1);

	/* TODO(crbug.com/496888): set up pinmux */
	gpio_config_module(MODULE_USB, 1);

	/* Make sure interrupts are disabled */
	GR_USB_GINTMSK = 0;
	GR_USB_DAINTMSK = 0;
	GR_USB_DIEPMSK = 0;
	GR_USB_DOEPMSK = 0;

	/* Select the correct PHY */
	GR_USB_GGPIO = GGPIO_WRITE(USB_CUSTOM_CFG_REG,
				   (USB_PHY_ACTIVE | USB_SEL_PHY0));

	/* Full-Speed Serial PHY */
	GR_USB_GUSBCFG = GUSBCFG_PHYSEL_FS | GUSBCFG_FSINTF_6PIN
		| GUSBCFG_TOUTCAL(7)
		/* FIXME: Magic number! 14 is for 15MHz! Use 9 for 30MHz */
		| GUSBCFG_USBTRDTIM(14);

	usb_softreset();

	GR_USB_GUSBCFG = GUSBCFG_PHYSEL_FS | GUSBCFG_FSINTF_6PIN
		| GUSBCFG_TOUTCAL(7)
		/* FIXME: Magic number! 14 is for 15MHz! Use 9 for 30MHz */
		| GUSBCFG_USBTRDTIM(14);

	/* Global + DMA configuration */
	/* TODO: What about the AHB Burst Length Field? It's 0 now. */
	GR_USB_GAHBCFG = GAHBCFG_DMA_EN | GAHBCFG_GLB_INTR_EN |
			 GAHBCFG_NP_TXF_EMP_LVL;

	/* Be in disconnected state until we are ready */
	usb_disconnect();

	/* Max speed: USB2 FS */
	GR_USB_DCFG = DCFG_DEVSPD_FS48 | DCFG_DESCDMA;

	/* Setup FIFO configuration */
	 setup_data_fifos();

	/* Device registers have been setup */
	GR_USB_DCTL |= DCTL_PWRONPRGDONE;
	udelay(10);
	GR_USB_DCTL &= ~DCTL_PWRONPRGDONE;

	/* Clear global NAKs */
	GR_USB_DCTL |= DCTL_CGOUTNAK | DCTL_CGNPINNAK;

	/* Clear any pending interrupts */
	for (i = 0; i < 16; i++) {
		GR_USB_DIEPINT(i) = 0xffffffff;
		GR_USB_DOEPINT(i) = 0xffffffff;
	}
	GR_USB_GINTSTS = 0xFFFFFFFF;

	/* Unmask some endpoint interrupt causes */
	GR_USB_DIEPMSK = DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK;
	GR_USB_DOEPMSK = DOEPMSK_EPDISBLDMSK | DOEPMSK_XFERCOMPLMSK |
		DOEPMSK_SETUPMSK;

	/* Enable interrupt handlers */
	task_enable_irq(GC_IRQNUM_USB0_USBINTR);

	/* Allow USB interrupts to come in */
	GR_USB_GINTMSK =
		/* NAK bits that must be cleared by the DCTL register */
		GINTMSK(GOUTNAKEFF) | GINTMSK(GINNAKEFF) |
		/* Initialization events */
		GINTMSK(USBRST) | GINTMSK(ENUMDONE) |
		/* Endpoint activity, cleared by the DOEPINT/DIEPINT regs */
		GINTMSK(OEPINT) | GINTMSK(IEPINT) |
		/* Reset detected while suspended. Need to wake up. */
		GINTMSK(RESETDET) |
		/* Idle, Suspend detected. Should go to sleep. */
		GINTMSK(ERLYSUSP) | GINTMSK(USBSUSP);

#ifndef CONFIG_USB_INHIBIT_CONNECT
	/* Indicate our presence to the USB host */
	usb_connect();
#endif

	print_later("usb_init() done", 0, 0, 0, 0, 0);
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
