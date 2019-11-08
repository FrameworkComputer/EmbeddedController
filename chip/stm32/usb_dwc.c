/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "usb_hw.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_descriptor.h"
#include "watchdog.h"


/****************************************************************************/
/* Debug output */

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

/* TODO: Something unexpected happened. Figure out how to report & fix it. */
#define report_error(val)						\
	CPRINTS("Unhandled USB event at %s line %d: 0x%x",		\
		__FILE__, __LINE__, val)


/****************************************************************************/
/* Standard USB stuff */

#ifdef CONFIG_USB_BOS
/* v2.10 (vs 2.00) BOS Descriptor provided */
#define USB_DEV_BCDUSB 0x0210
#else
#define USB_DEV_BCDUSB 0x0200
#endif

#ifndef USB_DEV_CLASS
#define USB_DEV_CLASS USB_CLASS_PER_INTERFACE
#endif

#ifndef CONFIG_USB_BCD_DEV
#define CONFIG_USB_BCD_DEV 0x0100		/* 1.00 */
#endif

#ifndef CONFIG_USB_SERIALNO
#define USB_STR_SERIALNO 0
#else
static int usb_load_serial(void);
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
	.iSerialNumber = USB_STR_SERIALNO,
	.bNumConfigurations = 1
};

/* USB Configuration Descriptor */
const struct usb_config_descriptor USB_CONF_DESC(conf) = {
	.bLength = USB_DT_CONFIG_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0x0BAD,	 /* number of returned bytes, set at runtime */
	.bNumInterfaces = USB_IFACE_COUNT,
	.bConfigurationValue = 1,		/* Caution: hard-coded value */
	.iConfiguration = USB_STR_VERSION,
	.bmAttributes = 0x80 /* Reserved bit */
#ifdef CONFIG_USB_SELF_POWERED  /* bus or self powered */
		      | 0x40
#endif
#ifdef CONFIG_USB_REMOTE_WAKEUP
		      | 0x20
#endif
	,
	.bMaxPower = (CONFIG_USB_MAXPOWER_MA / 2),
};

const uint8_t usb_string_desc[] = {
	4,					/* Descriptor size */
	USB_DT_STRING,
	0x09, 0x04			    /* LangID = 0x0409: U.S. English */
};

/****************************************************************************/
/* Packet-handling stuff, specific to this SoC */

/* Some internal state to keep track of what's going on */
static enum {
	WAITING_FOR_SETUP_PACKET,
	DATA_STAGE_IN,
	NO_DATA_STAGE,
} what_am_i_doing;

#ifdef DEBUG_ME
static const char * const wat[3] = {
	[WAITING_FOR_SETUP_PACKET] = "wait_for_setup",
	[DATA_STAGE_IN] = "data_in",
	[NO_DATA_STAGE] = "no_data",
};
#endif

/* Programmer's Guide, Table 10-7 */
enum table_case {
	BAD_0,
	TABLE_CASE_COMPLETE,
	TABLE_CASE_SETUP,
	TABLE_CASE_WTF,
	TABLE_CASE_D,
	TABLE_CASE_E,
	BAD_6,
	BAD_7,
};

static enum table_case decode_table_10_7(uint32_t doepint)
{
	enum table_case val = BAD_0;

	/* Bits: SI, SPD, IOC */
	if (doepint & DOEPINT_XFERCOMPL)
		val += 1;
	if (doepint & DOEPINT_SETUP)
		val += 2;
	return val;
}

/* For STATUS/OUT: Use two DMA descriptors, each with one-packet buffers */
#define NUM_OUT_BUFFERS 2
static uint8_t __aligned(4) ep0_setup_buf[USB_MAX_PACKET_SIZE];

/* For IN: Several DMA descriptors, all pointing into one large buffer, so that
 * we can return the configuration descriptor as one big blob.
 */
#define NUM_IN_PACKETS_AT_ONCE 4
#define IN_BUF_SIZE (NUM_IN_PACKETS_AT_ONCE * USB_MAX_PACKET_SIZE)
static uint8_t __aligned(4) ep0_in_buf[IN_BUF_SIZE];

struct dwc_usb_ep ep0_ctl = {
	.max_packet = USB_MAX_PACKET_SIZE,
	.tx_fifo = 0,
	.out_pending = 0,
	.out_expected = 0,
	.out_data = 0,
	.out_databuffer = ep0_setup_buf,
	.out_databuffer_max = sizeof(ep0_setup_buf),
	.rx_deferred = 0,
	.in_packets = 0,
	.in_pending = 0,
	.in_data = 0,
	.in_databuffer = ep0_in_buf,
	.in_databuffer_max = sizeof(ep0_in_buf),
	.tx_deferred = 0,
};

/* Overall device state (USB 2.0 spec, section 9.1.1).
 * We only need a few, though.
 */
static enum {
	DS_DEFAULT,
	DS_ADDRESS,
	DS_CONFIGURED,
} device_state;
static uint8_t configuration_value;


/* True if the HW Rx/OUT FIFO is currently listening. */
int rx_ep_is_active(uint32_t ep_num)
{
	return (GR_USB_DOEPCTL(ep_num) & DXEPCTL_EPENA) ? 1 : 0;
}

/* Number of bytes the HW Rx/OUT FIFO has for us.
 *
 * @param ep_num	USB endpoint
 *
 * @returns		number of bytes ready, zero if none.
 */
int rx_ep_pending(uint32_t ep_num)
{
	struct dwc_usb_ep *ep = usb_ctl.ep[ep_num];

	return ep->out_pending;
}

/* True if the Tx/IN FIFO can take some bytes from us. */
int tx_ep_is_ready(uint32_t ep_num)
{
	struct dwc_usb_ep *ep = usb_ctl.ep[ep_num];
	int ready;

	/* Is the tx hw idle? */
	ready = !(GR_USB_DIEPCTL(ep_num) & DXEPCTL_EPENA);

	/* Is there no pending data? */
	ready &= (ep->in_pending == 0);
	return ready;
}

/* Write packets of data IN to the host.
 *
 * This function uses DMA, so the *data write buffer
 * must persist until the write completion event.
 *
 * @param ep_num	USB endpoint to write
 * @param len		number of bytes to write
 * @param data		pointer of data to write
 *
 * @return		bytes written
 */
int usb_write_ep(uint32_t ep_num, int len, void *data)
{
	struct dwc_usb_ep *ep = usb_ctl.ep[ep_num];

	if (GR_USB_DIEPCTL(ep_num) & DXEPCTL_EPENA) {
		CPRINTS("usb_write_ep ep%d: FAIL: tx already in progress!",
			ep_num);
		return 0;
	}

	/* We will send as many packets as necessary, including a final
	 * packet of < USB_MAX_PACKET_SIZE (maybe zero length)
	 */
	ep->in_packets = (len + USB_MAX_PACKET_SIZE - 1) / USB_MAX_PACKET_SIZE;
	ep->in_pending = len;
	ep->in_data = data;

	GR_USB_DIEPTSIZ(ep_num) = 0;

	GR_USB_DIEPTSIZ(ep_num) |= DXEPTSIZ_PKTCNT(ep->in_packets);
	GR_USB_DIEPTSIZ(ep_num) |= DXEPTSIZ_XFERSIZE(len);
	GR_USB_DIEPDMA(ep_num) = (uint32_t)(ep->in_data);

	/* We could support longer multi-dma transfers here. */
	ep->in_pending -= len;
	ep->in_packets -= ep->in_packets;
	ep->in_data += len;

	/* We are ready to enable this endpoint to start transferring data. */
	GR_USB_DIEPCTL(ep_num) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
	return len;
}

/* Tx/IN interrupt handler */
void usb_epN_tx(uint32_t ep_num)
{
	struct dwc_usb_ep *ep = usb_ctl.ep[ep_num];
	uint32_t dieptsiz = GR_USB_DIEPTSIZ(ep_num);

	if (GR_USB_DIEPCTL(ep_num) & DXEPCTL_EPENA) {
		CPRINTS("usb_epN_tx ep%d: tx still active.", ep_num);
		return;
	}

	/* clear the Tx/IN interrupts */
	GR_USB_DIEPINT(ep_num) = 0xffffffff;

	/*
	 * Let's assume this is actually true.
	 * We could support multi-dma transfers here.
	 */
	ep->in_packets = 0;
	ep->in_pending = dieptsiz & GC_USB_DIEPTSIZ1_XFERSIZE_MASK;

	if (ep->tx_deferred)
		hook_call_deferred(ep->tx_deferred, 0);
}

/* Read a packet of data OUT from the host.
 *
 * This function uses DMA, so the *data write buffer
 * must persist until the read completion event.
 *
 * @param ep_num	USB endpoint to read
 * @param len		number of bytes to read
 * @param data		pointer of data to read
 *
 * @return		EC_SUCCESS on success
 */
int usb_read_ep(uint32_t ep_num, int len, void *data)
{
	struct dwc_usb_ep *ep = usb_ctl.ep[ep_num];
	int packets = (len + USB_MAX_PACKET_SIZE - 1) / USB_MAX_PACKET_SIZE;

	ep->out_data = data;
	ep->out_pending = 0;
	ep->out_expected = len;

	GR_USB_DOEPTSIZ(ep_num) = 0;
	GR_USB_DOEPTSIZ(ep_num) |= DXEPTSIZ_PKTCNT(packets);
	GR_USB_DOEPTSIZ(ep_num) |= DXEPTSIZ_XFERSIZE(len);
	GR_USB_DOEPDMA(ep_num) = (uint32_t)ep->out_data;

	GR_USB_DOEPCTL(ep_num) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
	return EC_SUCCESS;
}

/* Rx/OUT endpoint interrupt handler */
void usb_epN_rx(uint32_t ep_num)
{
	struct dwc_usb_ep *ep = usb_ctl.ep[ep_num];

	/* Still receiving data. Let's wait. */
	if (rx_ep_is_active(ep_num))
		return;

	/* Bytes received decrement DOEPTSIZ XFERSIZE */
	if (GR_USB_DOEPINT(ep_num) & DOEPINT_XFERCOMPL) {
		if (ep->out_expected > 0) {
			ep->out_pending =
				ep->out_expected -
				(GR_USB_DOEPTSIZ(ep_num) &
				 GC_USB_DOEPTSIZ1_XFERSIZE_MASK);
		} else {
			CPRINTF("usb_ep%d_rx: unexpected RX DOEPTSIZ %08x\n",
				ep_num, GR_USB_DOEPTSIZ(ep_num));
			ep->out_pending = 0;
		}
		ep->out_expected = 0;
		GR_USB_DOEPTSIZ(ep_num) = 0;
	}

	/* clear the RX/OUT interrupts */
	GR_USB_DOEPINT(ep_num) = 0xffffffff;

	if (ep->rx_deferred)
		hook_call_deferred(ep->rx_deferred, 0);
}

/* Reset endpoint HW block. */
void epN_reset(uint32_t ep_num)
{
	GR_USB_DOEPCTL(ep_num) = DXEPCTL_MPS(USB_MAX_PACKET_SIZE) |
		DXEPCTL_USBACTEP | DXEPCTL_EPTYPE_BULK;
	GR_USB_DIEPCTL(ep_num) = DXEPCTL_MPS(USB_MAX_PACKET_SIZE) |
		DXEPCTL_USBACTEP | DXEPCTL_EPTYPE_BULK |
		DXEPCTL_TXFNUM(ep_num);
	GR_USB_DAINTMSK |= DAINT_INEP(ep_num) |
			   DAINT_OUTEP(ep_num);
}


/******************************************************************************
 * Internal and EP0 functions.
 */


static void flush_all_fifos(void)
{
	/* Flush all FIFOs according to Section 2.1.1.2 */
	GR_USB_GRSTCTL = GRSTCTL_TXFNUM(0x10) | GRSTCTL_TXFFLSH
		| GRSTCTL_RXFFLSH;
	while (GR_USB_GRSTCTL & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH))
		;
}

int send_in_packet(uint32_t ep_num)
{
	struct dwc_usb *usb = &usb_ctl;
	struct dwc_usb_ep *ep = usb->ep[ep_num];
	int len = MIN(USB_MAX_PACKET_SIZE, ep->in_pending);

	if (ep->in_packets == 0) {
		report_error(ep_num);
		return -1;
	}

	GR_USB_DIEPTSIZ(ep_num) = 0;

	GR_USB_DIEPTSIZ(ep_num) |= DXEPTSIZ_PKTCNT(1);
	GR_USB_DIEPTSIZ(0) |= DXEPTSIZ_XFERSIZE(len);
	GR_USB_DIEPDMA(0) = (uint32_t)ep->in_data;


	/* We're sending this much. */
	ep->in_pending -= len;
	ep->in_packets -= 1;
	ep->in_data += len;

	/* We are ready to enable this endpoint to start transferring data. */
	return len;
}


/* Load the EP0 IN FIFO buffer with some data (zero-length works too). Returns
 * len, or negative on error.
 */
int initialize_in_transfer(const void *source, uint32_t len)
{
	struct dwc_usb *usb = &usb_ctl;
	struct dwc_usb_ep *ep = usb->ep[0];

#ifdef CONFIG_USB_DWC_FS
	/* FS OTG port does not support DMA or external phy */
	ASSERT(!(usb->dma_en));
	ASSERT(usb->phy_type == USB_PHY_INTERNAL);
	ASSERT(usb->speed == USB_SPEED_FS);
	ASSERT(usb->irq == STM32_IRQ_OTG_FS);
#else
	/* HS OTG port requires an external phy to support HS */
	ASSERT(!((usb->phy_type == USB_PHY_INTERNAL) &&
		(usb->speed == USB_SPEED_HS)));
	ASSERT(usb->irq == STM32_IRQ_OTG_HS);
#endif

	/* Copy the data into our FIFO buffer */
	if (len >= IN_BUF_SIZE) {
		report_error(len);
		return -1;
	}

	/* Stage data in DMA buffer. */
	memcpy(ep->in_databuffer, source, len);
	ep->in_data = ep->in_databuffer;

	/* We will send as many packets as necessary, including a final
	 * packet of < USB_MAX_PACKET_SIZE (maybe zero length)
	 */
	ep->in_packets = (len + USB_MAX_PACKET_SIZE)/USB_MAX_PACKET_SIZE;
	ep->in_pending = len;

	send_in_packet(0);
	return len;
}

/* Prepare the EP0 OUT FIFO buffer to accept some data. Returns len, or
 * negative on error.
 */
int accept_out_fifo(uint32_t len)
{
	/* TODO: This is not yet implemented */
	report_error(len);
	return -1;
}

/* The next packet from the host should be a Setup packet. Get ready for it. */
static void expect_setup_packet(void)
{
	struct dwc_usb *usb = &usb_ctl;
	struct dwc_usb_ep *ep = usb->ep[0];

	what_am_i_doing = WAITING_FOR_SETUP_PACKET;
	ep->out_data = ep->out_databuffer;

	/* We don't care about IN packets right now, only OUT. */
	GR_USB_DAINTMSK |= DAINT_OUTEP(0);
	GR_USB_DAINTMSK &= ~DAINT_INEP(0);

	GR_USB_DOEPTSIZ(0) = 0;
	GR_USB_DOEPTSIZ(0) |= DXEPTSIZ_PKTCNT(1);
	GR_USB_DOEPTSIZ(0) |= DXEPTSIZ_XFERSIZE(0x18);
	GR_USB_DOEPTSIZ(0) |= DXEPTSIZ_SUPCNT(1);
	GR_USB_DOEPCTL(0) = DXEPCTL_USBACTEP | DXEPCTL_EPENA;
	GR_USB_DOEPDMA(0) = (uint32_t)ep->out_data;
}

/* We're complaining about something by stalling both IN and OUT packets,
 * but a SETUP packet will get through anyway, so prepare for it.
 */
static void stall_both_fifos(void)
{
	what_am_i_doing = WAITING_FOR_SETUP_PACKET;
	/* We don't care about IN packets right now, only OUT. */
	GR_USB_DAINTMSK |= DAINT_OUTEP(0);
	GR_USB_DAINTMSK &= ~DAINT_INEP(0);

	GR_USB_DOEPCTL(0) |= DXEPCTL_STALL;
	GR_USB_DIEPCTL(0) |= DXEPCTL_STALL;
	expect_setup_packet();
}

/* The TX FIFO buffer is loaded. Start the Data phase. */
static void expect_data_phase_in(enum table_case tc)
{
	what_am_i_doing = DATA_STAGE_IN;

	/* Send the reply (data phase in) */
	if (tc == TABLE_CASE_SETUP)
		GR_USB_DIEPCTL(0) |= DXEPCTL_USBACTEP |
					DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DIEPCTL(0) |= DXEPCTL_EPENA;

	/* We'll receive an empty packet back as a ack, I guess. */
	if (tc == TABLE_CASE_SETUP)
		GR_USB_DOEPCTL(0) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DOEPCTL(0) |= DXEPCTL_EPENA;

	/* Get an interrupt when either IN or OUT arrives */
	GR_USB_DAINTMSK |= (DAINT_OUTEP(0) | DAINT_INEP(0));

}

static void expect_data_phase_out(enum table_case tc)
{
	/* TODO: This is not yet supported */
	report_error(tc);
	expect_setup_packet();
}

/* No Data phase, just Status phase (which is IN, since Setup is OUT) */
static void expect_status_phase_in(enum table_case tc)
{
	what_am_i_doing = NO_DATA_STAGE;

	/* Expect a zero-length IN for the Status phase */
	(void) initialize_in_transfer(0, 0);

	/* Blindly following instructions here, too. */
	if (tc == TABLE_CASE_SETUP)
		GR_USB_DIEPCTL(0) |= DXEPCTL_USBACTEP
			| DXEPCTL_CNAK | DXEPCTL_EPENA;
	else
		GR_USB_DIEPCTL(0) |= DXEPCTL_EPENA;

	/* Get an interrupt when either IN or OUT arrives */
	GR_USB_DAINTMSK |= (DAINT_OUTEP(0) | DAINT_INEP(0));
}

/* Handle a Setup packet that expects us to send back data in reply. Return the
 * length of the data we're returning, or negative to indicate an error.
 */
static int handle_setup_with_in_stage(enum table_case tc,
				      struct usb_setup_packet *req)
{
	struct dwc_usb *usb = &usb_ctl;
	struct dwc_usb_ep *ep = usb->ep[0];

	const void *data = 0;
	uint32_t len = 0;
	int ugly_hack = 0;
	static const uint16_t zero;		/* == 0 */

	switch (req->bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		uint8_t type = req->wValue >> 8;
		uint8_t idx = req->wValue & 0xff;

		switch (type) {
		case USB_DT_DEVICE:
			data = &dev_desc;
			len = sizeof(dev_desc);
			break;
		case USB_DT_CONFIGURATION:
			data = __usb_desc;
			len = USB_DESC_SIZE;
			ugly_hack = 1;		/* see below */
			break;
#ifdef CONFIG_USB_BOS
		case USB_DT_BOS:
			data = bos_ctx.descp;
			len = bos_ctx.size;
			break;
#endif
		case USB_DT_STRING:
			if (idx >= USB_STR_COUNT)
				return -1;
#ifdef CONFIG_USB_SERIALNO
			if (idx == USB_STR_SERIALNO)
				data = (uint8_t *)usb_serialno_desc;
			else
#endif
				data = usb_strings[idx];
			len = *(uint8_t *)data;
			break;
		case USB_DT_DEVICE_QUALIFIER:
			/* We're not high speed */
			return -1;
		case USB_DT_DEBUG:
			/* Not supported */
			return -1;
		default:
			report_error(type);
			return -1;
		}
		break;
	}
	case USB_REQ_GET_STATUS: {
		/* TODO: Device Status: Remote Wakeup? Self Powered? */
		data = &zero;
		len = sizeof(zero);
		break;
	}
	case USB_REQ_GET_CONFIGURATION:
		data = &configuration_value;
		len = sizeof(configuration_value);
		break;

	case USB_REQ_SYNCH_FRAME:
		/* Unimplemented */
		return -1;

	default:
		report_error(req->bRequest);
		return -1;
	}

	/* Don't send back more than we were asked for. */
	len = MIN(req->wLength, len);

	/* Prepare the TX FIFO. If we haven't preallocated enough room in the
	 * TX FIFO for the largest reply, we'll have to stall. This is a bug in
	 * our code, but detecting it easily at compile time is related to the
	 * ugly_hack directly below.
	 */
	if (initialize_in_transfer(data, len) < 0)
		return -1;

	if (ugly_hack) {
		/*
		 * TODO: Somebody figure out how to fix this, please.
		 *
		 * The USB configuration descriptor request is unique in that
		 * it not only returns the configuration descriptor, but also
		 * all the interface descriptors and all their endpoint
		 * descriptors as one enormous blob. We've set up some macros
		 * so we can declare and implement separate interfaces in
		 * separate files just by compiling them, and all the relevant
		 * descriptors are sorted and bundled up by the linker. But the
		 * total length of the entire blob needs to appear in the first
		 * configuration descriptor struct and because we don't know
		 * that value until after linking, it can't be initialized as a
		 * constant. So we have to compute it at run-time and shove it
		 * in here, which also means that we have to copy the whole
		 * blob into our TX FIFO buffer so that it's mutable. Otherwise
		 * we could just point at it (or pretty much any other constant
		 * struct that we wanted to send to the host). Bah.
		 */
		struct usb_config_descriptor *cfg =
			(struct usb_config_descriptor *)ep->in_databuffer;
		/* set the real descriptor size */
		cfg->wTotalLength = USB_DESC_SIZE;
	}

	return len;
}

/* Handle a Setup that comes with additional data for us. */
static int handle_setup_with_out_stage(enum table_case tc,
					struct usb_setup_packet *req)
{
	/* TODO: We don't support any of these. We should. */
	report_error(-1);
	return -1;
}

/* Some Setup packets don't have a data stage at all. */
static int handle_setup_with_no_data_stage(enum table_case tc,
					   struct usb_setup_packet *req)
{
	uint8_t set_addr;

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
		CPRINTS("SETAD 0x%02x (%d)", set_addr, set_addr);
		device_state = DS_ADDRESS;
		break;

	case USB_REQ_SET_CONFIGURATION:
		switch (req->wValue) {
		case 0:
			configuration_value = req->wValue;
			device_state = DS_ADDRESS;
			break;
		case 1:	    /* Caution: Only one config descriptor TODAY */
			/* TODO: All endpoints set to DATA0 toggle state */
			configuration_value = req->wValue;
			device_state = DS_CONFIGURED;
			break;
		default:
			/* Nope. That's a paddlin. */
			report_error(-1);
			return -1;
		}
		break;

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		/* TODO: Handle DEVICE_REMOTE_WAKEUP, ENDPOINT_HALT? */
		break;

	default:
		/* Anything else is unsupported */
		report_error(-1);
		return -1;
	}

	/* No data to transfer, go straight to the Status phase. */
	return 0;
}

/* Dispatch an incoming Setup packet according to its type */
static void handle_setup(enum table_case tc)
{
	struct dwc_usb *usb = &usb_ctl;
	struct dwc_usb_ep *ep = usb->ep[0];
	struct usb_setup_packet *req =
		(struct usb_setup_packet *)ep->out_databuffer;
	int data_phase_in = req->bmRequestType & USB_DIR_IN;
	int data_phase_out = !data_phase_in && req->wLength;
	int bytes = -1;				/* default is to stall */

	if (0 == (req->bmRequestType & (USB_TYPE_MASK | USB_RECIP_MASK))) {
		/* Standard Device requests */
		if (data_phase_in)
			bytes = handle_setup_with_in_stage(tc, req);
		else if (data_phase_out)
			bytes = handle_setup_with_out_stage(tc, req);
		else
			bytes = handle_setup_with_no_data_stage(tc, req);
	} else if (USB_RECIP_INTERFACE ==
		   (req->bmRequestType & USB_RECIP_MASK)) {
		/* Interface-specific requests */
		uint8_t iface = req->wIndex & 0xff;

		if (iface < USB_IFACE_COUNT)
			bytes = usb_iface_request[iface](req);
	} else {
		/* Something we need to add support for? */
		report_error(-1);
	}

	/* We say "no" to unsupported and intentionally unhandled requests by
	 * stalling the Data and/or Status stage.
	 */
	if (bytes < 0) {
		/* Stall both IN and OUT. SETUP will come through anyway. */
		stall_both_fifos();
	} else {
		if (data_phase_in)
			expect_data_phase_in(tc);
		else if (data_phase_out)
			expect_data_phase_out(tc);
		else
			expect_status_phase_in(tc);
	}
}

/* This handles both IN and OUT interrupts for EP0 */
static void ep0_interrupt(uint32_t intr_on_out, uint32_t intr_on_in)
{
	struct dwc_usb *usb = &usb_ctl;
	struct dwc_usb_ep *ep = usb->ep[0];
	uint32_t doepint, diepint;
	enum table_case tc;
	int out_complete, out_setup, in_complete;

	/* Determine the interrupt cause and clear the bits quickly, but only
	 * if they really apply. I don't think they're trustworthy if we didn't
	 * actually get an interrupt.
	 */
	doepint = GR_USB_DOEPINT(0) & GR_USB_DOEPMSK;
	if (intr_on_out)
		GR_USB_DOEPINT(0) = doepint;
	diepint = GR_USB_DIEPINT(0) & GR_USB_DIEPMSK;
	if (intr_on_in)
		GR_USB_DIEPINT(0) = diepint;

	out_complete = doepint & DOEPINT_XFERCOMPL;
	out_setup = doepint & DOEPINT_SETUP;
	in_complete = diepint & DIEPINT_XFERCOMPL;

	/* Decode the situation according to Table 10-7 */
	tc = decode_table_10_7(doepint);

	switch (what_am_i_doing) {
	case WAITING_FOR_SETUP_PACKET:
		if (out_setup)
			handle_setup(tc);
		else
			report_error(-1);
		break;

	case DATA_STAGE_IN:
		if (intr_on_in && in_complete) {
			/* A packet is sent. Should we send another? */
			if (ep->in_packets > 0) {
				/* Send another packet. */
				send_in_packet(0);
				expect_data_phase_in(tc);
			}
		}

		/* But we should ignore the OUT endpoint if we didn't actually
		 * get an OUT interrupt.
		 */
		if (!intr_on_out)
			break;

		if (out_setup) {
			/* The first IN packet has been seen. Keep going. */
			break;
		}
		if (out_complete) {
			/* We've handled the Status phase. All done. */
			expect_setup_packet();
			break;
		}

		/* Anything else should be ignorable. Right? */
		break;

	case NO_DATA_STAGE:
		if (intr_on_in && in_complete) {
			/* We are not expecting an empty packet in
			 * return for our empty packet.
			 */
			expect_setup_packet();
		}

		/* Done unless we got an OUT interrupt */
		if (!intr_on_out)
			break;

		if (out_setup) {
			report_error(-1);
			break;
		}

		/* Anything else means get ready for a Setup packet */
		report_error(-1);
		expect_setup_packet();
		break;
	}
}

/****************************************************************************/
/* USB device initialization and shutdown routines */

/*
 * DATA FIFO Setup. There is an internal SPRAM used to buffer the IN/OUT
 * packets and track related state without hammering the AHB and system RAM
 * during USB transactions. We have to specify where and how much of that SPRAM
 * to use for what.
 *
 * See Programmer's Guide chapter 2, "Calculating FIFO Size".
 * We're using Dedicated TxFIFO Operation, without enabling thresholding.
 *
 * Section 2.1.1.2, page 30: RXFIFO size is the same as for Shared FIFO, which
 * is Section 2.1.1.1, page 28. This is also the same as Method 2 on page 45.
 *
 * We support up to 3 control EPs, no periodic IN EPs, up to 16 TX EPs. Max
 * data packet size is 64 bytes. Total SPRAM available is 1024 slots.
 */
#define MAX_CONTROL_EPS   3
#define MAX_NORMAL_EPS    16
#define FIFO_RAM_DEPTH    1024
/*
 * Device RX FIFO size is thus:
 *   (4 * 3 + 6) + 2 * ((64 / 4) + 1) + (2 * 16) + 1 == 85
 */
#define RXFIFO_SIZE  ((4 * MAX_CONTROL_EPS + 6) + \
		      2 * ((USB_MAX_PACKET_SIZE / 4) + 1) + \
		      (2 * MAX_NORMAL_EPS) + 1)
/*
 * Device TX FIFO size is 2 * (64 / 4) == 32 for each IN EP (Page 46).
 */
#define TXFIFO_SIZE  (2 * (USB_MAX_PACKET_SIZE / 4))
/*
 * We need 4 slots per endpoint direction for endpoint status stuff (Table 2-1,
 * unconfigurable).
 */
#define EP_STATUS_SIZE (4 * MAX_NORMAL_EPS * 2)
/*
 * Make sure all that fits.
 */
BUILD_ASSERT(RXFIFO_SIZE + TXFIFO_SIZE * MAX_NORMAL_EPS + EP_STATUS_SIZE <
	     FIFO_RAM_DEPTH);


/* Now put those constants into the correct registers */
static void setup_data_fifos(void)
{
	int i;

	/* Programmer's Guide, p31 */
	GR_USB_GRXFSIZ = RXFIFO_SIZE;			      /* RXFIFO */
	GR_USB_GNPTXFSIZ = (TXFIFO_SIZE << 16) | RXFIFO_SIZE; /* TXFIFO 0 */

	/* TXFIFO 1..15 */
	for (i = 1; i < MAX_NORMAL_EPS; i++)
		GR_USB_DIEPTXF(i) = ((TXFIFO_SIZE << 16) |
				     (RXFIFO_SIZE + i * TXFIFO_SIZE));

	/*
	 * TODO: The Programmer's Guide is confusing about when or whether to
	 * flush the FIFOs. Section 2.1.1.2 (p31) just says to flush. Section
	 * 2.2.2 (p55) says to stop all the FIFOs first, then flush. Section
	 * 7.5.4 (p162) says that flushing the RXFIFO at reset is not
	 * recommended at all.
	 *
	 * I'm also unclear on whether or not the individual EPs are expected
	 * to be disabled already (DIEPCTLn/DOEPCTLn.EPENA == 0), and if so,
	 * whether by firmware or hardware.
	 */

	/* Flush all FIFOs according to Section 2.1.1.2 */
	GR_USB_GRSTCTL = GRSTCTL_TXFNUM(0x10) | GRSTCTL_TXFFLSH
		| GRSTCTL_RXFFLSH;
	while (GR_USB_GRSTCTL & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH))
		;				/* TODO: timeout 100ms */
}

static void usb_init_endpoints(void)
{
	int ep;

	/* Prepare to receive packets on EP0 */
	expect_setup_packet();

	/* Reset the other endpoints */
	for (ep = 1; ep < USB_EP_COUNT; ep++)
		usb_ep_event[ep](USB_EVENT_RESET);
}

static void usb_reset(void)
{
	/* Clear our internal state */
	device_state = DS_DEFAULT;
	configuration_value = 0;

	/* Clear the device address */
	GWRITE_FIELD(USB, DCFG, DEVADDR, 0);

	/* Reinitialize all the endpoints */
	usb_init_endpoints();
}

static void usb_resetdet(void)
{
	/* TODO: Same as normal reset, right? I think we only get this if we're
	 * suspended (sleeping) and the host resets us. Try it and see.
	 */
	usb_reset();
}

static void usb_enumdone(void)
{
	/* We can change to HS here. We will not go to HS today */
	GR_USB_DCTL |= DCTL_CGOUTNAK;
}


void usb_interrupt(void)
{
	uint32_t status = GR_USB_GINTSTS & GR_USB_GINTMSK;
	uint32_t oepint = status & GINTSTS(OEPINT);
	uint32_t iepint = status & GINTSTS(IEPINT);
	int ep;

	if (status & GINTSTS(ENUMDONE))
		usb_enumdone();

	if (status & GINTSTS(RESETDET))
		usb_resetdet();

	if (status & GINTSTS(USBRST))
		usb_reset();

	/* Endpoint interrupts */
	if (oepint || iepint) {
		/* Note: It seems that the DAINT bits are only trustworthy for
		 * identifying interrupts when selected by the corresponding
		 * OEPINT and IEPINT bits from GINTSTS.
		 */
		uint32_t daint = GR_USB_DAINT;

		/* EP0 has a combined IN/OUT handler. Only call it once, but
		 * let it know which direction(s) had an interrupt.
		 */
		if (daint & (DAINT_OUTEP(0) | DAINT_INEP(0))) {
			uint32_t intr_on_out = (oepint &&
						(daint & DAINT_OUTEP(0)));
			uint32_t intr_on_in = (iepint &&
						(daint & DAINT_INEP(0)));
			ep0_interrupt(intr_on_out, intr_on_in);
		}

		/* Invoke the unidirectional IN and OUT functions for the other
		 * endpoints. Each handler must clear their own bits in
		 * DIEPINTn/DOEPINTn.
		 */
		for (ep = 1; ep < USB_EP_COUNT; ep++) {
			if (oepint && (daint & DAINT_OUTEP(ep)))
				usb_ep_rx[ep]();
			if (iepint && (daint & DAINT_INEP(ep)))
				usb_ep_tx[ep]();
		}
	}

	GR_USB_GINTSTS = status;
}
DECLARE_IRQ(STM32_IRQ_OTG_FS, usb_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_OTG_HS, usb_interrupt, 1);

static void usb_softreset(void)
{
	int timeout;

	CPRINTS("%s", __func__);

	/* Wait for bus idle */
	timeout = 10000;
	while (!(GR_USB_GRSTCTL & GRSTCTL_AHBIDLE) && timeout-- > 0)
		;

	/* Reset and wait for clear */
	GR_USB_GRSTCTL = GRSTCTL_CSFTRST;
	timeout = 10000;
	while ((GR_USB_GRSTCTL & GRSTCTL_CSFTRST) && timeout-- > 0)
		;
	if (GR_USB_GRSTCTL & GRSTCTL_CSFTRST) {
		CPRINTF("USB: reset failed\n");
		return;
	}

	/* Some more idle? */
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
	GR_USB_DCTL &= ~DCTL_SFTDISCON;
}

void usb_disconnect(void)
{
	GR_USB_DCTL |= DCTL_SFTDISCON;

	device_state = DS_DEFAULT;
	configuration_value = 0;
}

void usb_reset_init_phy(void)
{
	struct dwc_usb *usb = &usb_ctl;

	if (usb->phy_type == USB_PHY_ULPI) {
		GR_USB_GCCFG &= ~GCCFG_PWRDWN;
		GR_USB_GUSBCFG &= ~(GUSBCFG_TSDPS |
			GUSBCFG_ULPIFSLS | GUSBCFG_PHYSEL);
		GR_USB_GUSBCFG &= ~(GUSBCFG_ULPIEVBUSD | GUSBCFG_ULPIEVBUSI);
		/* No suspend */
		GR_USB_GUSBCFG |= GUSBCFG_ULPICSM | GUSBCFG_ULPIAR;

		usb_softreset();
	} else {
		GR_USB_GUSBCFG |= GUSBCFG_PHYSEL;
		usb_softreset();
		GR_USB_GCCFG |= GCCFG_PWRDWN;
	}
}

void usb_init(void)
{
	int i;
	struct dwc_usb *usb = &usb_ctl;

	CPRINTS("%s", __func__);

#ifdef CONFIG_USB_SERIALNO
	usb_load_serial();
#endif

	/* USB is in use */
	disable_sleep(SLEEP_MASK_USB_DEVICE);

	/* Enable clocks */
	clock_enable_module(MODULE_USB, 0);
	clock_enable_module(MODULE_USB, 1);

	/* TODO(crbug.com/496888): set up pinmux */
	gpio_config_module(MODULE_USB, 1);

	/* Make sure interrupts are disabled */
	GR_USB_GINTMSK = 0;
	GR_USB_DAINTMSK = 0;
	GR_USB_DIEPMSK = 0;
	GR_USB_DOEPMSK = 0;

	/* Full-Speed Serial PHY */
	usb_reset_init_phy();

	/* Global + DMA configuration */
	GR_USB_GAHBCFG = GAHBCFG_GLB_INTR_EN;
	GR_USB_GAHBCFG |= GAHBCFG_HBSTLEN_INCR4;
	if (usb->dma_en)
		GR_USB_GAHBCFG |= GAHBCFG_DMA_EN;

	/* Device only, no SRP */
	GR_USB_GUSBCFG |= GUSBCFG_FDMOD;
	GR_USB_GUSBCFG |= GUSBCFG_SRPCAP | GUSBCFG_HNPCAP;

	GR_USB_GCCFG &= ~GCCFG_VBDEN;
	GR_USB_GOTGCTL |= GOTGCTL_BVALOEN;
	GR_USB_GOTGCTL |= GOTGCTL_BVALOVAL;

	GR_USB_PCGCCTL = 0;

	if (usb->phy_type == USB_PHY_ULPI) {
		/* TODO(nsanders): add HS support like so.
		 * GR_USB_DCFG = (GR_USB_DCFG & ~GC_USB_DCFG_DEVSPD_MASK)
		 * | DCFG_DEVSPD_HSULPI;
		 */
		GR_USB_DCFG = (GR_USB_DCFG & ~GC_USB_DCFG_DEVSPD_MASK)
			| DCFG_DEVSPD_FSULPI;
	} else {
		GR_USB_DCFG = (GR_USB_DCFG & ~GC_USB_DCFG_DEVSPD_MASK)
			| DCFG_DEVSPD_FS48;
	}

	GR_USB_DCFG |= DCFG_NZLSOHSK;

	flush_all_fifos();

	/* Clear pending interrupts again */
	GR_USB_GINTMSK = 0;
	GR_USB_DIEPMSK = 0;
	GR_USB_DOEPMSK = 0;
	GR_USB_DAINT = 0xffffffff;
	GR_USB_DAINTMSK = 0;

	/* TODO: What about the AHB Burst Length Field? It's 0 now. */
	GR_USB_GAHBCFG |= GAHBCFG_TXFELVL | GAHBCFG_PTXFELVL;

	/* Device only, no SRP */
	GR_USB_GUSBCFG |= GUSBCFG_FDMOD
		| GUSBCFG_TOUTCAL(7)
		/* FIXME: Magic number! 14 is for 15MHz! Use 9 for 30MHz */
		| GUSBCFG_USBTRDTIM(14);

	/* Be in disconnected state until we are ready */
	usb_disconnect();

	/* If we've restored a nonzero device address, update our state. */
	if (GR_USB_DCFG & GC_USB_DCFG_DEVADDR_MASK) {
		/* Caution: We only have one config TODAY, so there's no real
		 * difference between DS_CONFIGURED and DS_ADDRESS.
		 */
		device_state = DS_CONFIGURED;
		configuration_value = 1;
	} else {
		device_state = DS_DEFAULT;
		configuration_value = 0;
	}

	/* Now that DCFG.DesDMA is accurate, prepare the FIFOs */
	setup_data_fifos();

	usb_init_endpoints();

	/* Clear any pending interrupts */
	for (i = 0; i < 16; i++) {
		GR_USB_DIEPINT(i) = 0xffffffff;
		GR_USB_DIEPTSIZ(i) = 0;
		GR_USB_DOEPINT(i) = 0xffffffff;
		GR_USB_DOEPTSIZ(i) = 0;
	}

	if (usb->dma_en) {
		GR_USB_DTHRCTL = DTHRCTL_TXTHRLEN_6 | DTHRCTL_RXTHRLEN_6;
		GR_USB_DTHRCTL |= DTHRCTL_RXTHREN | DTHRCTL_ISOTHREN
			| DTHRCTL_NONISOTHREN;
		i = GR_USB_DTHRCTL;
	}

	GR_USB_GINTSTS = 0xFFFFFFFF;

	GR_USB_GAHBCFG |= GAHBCFG_GLB_INTR_EN | GAHBCFG_TXFELVL
		| GAHBCFG_PTXFELVL;

	if (!(usb->dma_en))
		GR_USB_GINTMSK |= GINTMSK(RXFLVL);

	/* Unmask some endpoint interrupt causes */
	GR_USB_DIEPMSK = DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK;
	GR_USB_DOEPMSK = DOEPMSK_EPDISBLDMSK | DOEPMSK_XFERCOMPLMSK |
		DOEPMSK_SETUPMSK;

	/* Enable interrupt handlers */
	task_enable_irq(usb->irq);

	/* Allow USB interrupts to come in */
	GR_USB_GINTMSK |=
		/* NAK bits that must be cleared by the DCTL register */
		GINTMSK(GOUTNAKEFF) | GINTMSK(GINNAKEFF) |
		/* Initialization events */
		GINTMSK(USBRST) | GINTMSK(ENUMDONE) |
		/* Reset detected while suspended. Need to wake up. */
		GINTMSK(RESETDET) |		/* TODO: Do we need this? */
		/* Idle, Suspend detected. Should go to sleep. */
		GINTMSK(ERLYSUSP) | GINTMSK(USBSUSP);

	GR_USB_GINTMSK |=
		/* Endpoint activity, cleared by the DOEPINT/DIEPINT regs */
		GINTMSK(OEPINT) | GINTMSK(IEPINT);

	/* Device registers have been setup */
	GR_USB_DCTL |= DCTL_PWRONPRGDONE;
	udelay(10);
	GR_USB_DCTL &= ~DCTL_PWRONPRGDONE;

	/* Clear global NAKs */
	GR_USB_DCTL |= DCTL_CGOUTNAK | DCTL_CGNPINNAK;

#ifndef CONFIG_USB_INHIBIT_CONNECT
	/* Indicate our presence to the USB host */
	usb_connect();
#endif
}
#ifndef CONFIG_USB_INHIBIT_INIT
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);
#endif

void usb_release(void)
{
	struct dwc_usb *usb = &usb_ctl;

	/* signal disconnect to host */
	usb_disconnect();

	/* disable interrupt handlers */
	task_disable_irq(usb->irq);

	/* disable clocks */
	clock_enable_module(MODULE_USB, 0);
	/* TODO: pin-mux */

	/* USB is off, so sleep whenever */
	enable_sleep(SLEEP_MASK_USB_DEVICE);
}

/* Print USB info and stats */
static void usb_info(void)
{
	struct dwc_usb *usb = &usb_ctl;
	int i;

	CPRINTF("USB settings: %s%s%s\n",
		usb->speed == USB_SPEED_FS ? "FS " : "HS ",
		usb->phy_type == USB_PHY_INTERNAL ? "Internal Phy " : "ULPI ",
		usb->dma_en ? "DMA " : "");

	for (i = 0; i < USB_EP_COUNT; i++) {
		CPRINTF("Endpoint %d activity: %s%s\n", i,
			rx_ep_is_active(i) ? "RX " : "",
			tx_ep_is_ready(i) ? "" : "TX ");
	}
}

static int command_usb(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp("on", argv[1]))
			usb_init();
		else if (!strcasecmp("off", argv[1]))
			usb_release();
		else if (!strcasecmp("info", argv[1]))
			usb_info();
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(usb, command_usb,
			"[on|off|info]",
			"Get/set the USB connection state and PHY selection");

#ifdef CONFIG_USB_SERIALNO
/* This will be subbed into USB_STR_SERIALNO. */
struct usb_string_desc *usb_serialno_desc =
	USB_WR_STRING_DESC(DEFAULT_SERIALNO);

/* Update serial number */
static int usb_set_serial(const char *serialno)
{
	struct usb_string_desc *sd = usb_serialno_desc;
	int i;

	if (!serialno)
		return EC_ERROR_INVAL;

	/* Convert into unicode usb string desc. */
	for (i = 0; i < CONFIG_SERIALNO_LEN; i++) {
		sd->_data[i] = serialno[i];
		if (serialno[i] == 0)
			break;
	}
	/* Count wchars (w/o null terminator) plus size & type bytes. */
	sd->_len = (i * 2) + 2;
	sd->_type = USB_DT_STRING;

	return EC_SUCCESS;
}

/* Retrieve serial number from pstate flash. */
static int usb_load_serial(void)
{
	const char *serialno;
	int rv;

	serialno = board_read_serial();
	if (!serialno)
		return EC_ERROR_ACCESS_DENIED;

	rv = usb_set_serial(serialno);
	return rv;
}

/* Save serial number into pstate region. */
static int usb_save_serial(const char *serialno)
{
	int rv;

	if (!serialno)
		return EC_ERROR_INVAL;

	/* Save this new serial number to flash. */
	rv = board_write_serial(serialno);
	if (rv)
		return rv;

	/* Load this new serial number to memory. */
	rv = usb_load_serial();
	return rv;
}

static int command_serialno(int argc, char **argv)
{
	struct usb_string_desc *sd = usb_serialno_desc;
	char buf[CONFIG_SERIALNO_LEN];
	int rv = EC_SUCCESS;
	int i;

	if (argc != 1) {
		if ((strcasecmp(argv[1], "set") == 0) &&
		    (argc == 3)) {
			ccprintf("Saving serial number\n");
			rv = usb_save_serial(argv[2]);
		} else if ((strcasecmp(argv[1], "load") == 0) &&
			   (argc == 2)) {
			ccprintf("Loading serial number\n");
			rv = usb_load_serial();
		} else
			return EC_ERROR_INVAL;
	}

	for (i = 0; i < CONFIG_SERIALNO_LEN; i++)
		buf[i] = sd->_data[i];
	ccprintf("Serial number: %s\n", buf);
	return rv;
}

DECLARE_CONSOLE_COMMAND(serialno, command_serialno,
	"load/set [value]",
	"Read and write USB serial number");
#endif  /* CONFIG_USB_SERIALNO */
