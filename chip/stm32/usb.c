/* Copyright 2013 The ChromiumOS Authors
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
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "util.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)

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
#define CONFIG_USB_BCD_DEV 0x0100 /* 1.00 */
#endif

#ifndef CONFIG_USB_SERIALNO
#define USB_STR_SERIALNO 0
#else
static int usb_load_serial(void);
#endif

#ifndef CONFIG_USB_MAX_CONTROL_PACKET_SIZE
#define EP0_MAX_PACKET_SIZE USB_MAX_PACKET_SIZE
#else
#define EP0_MAX_PACKET_SIZE CONFIG_USB_MAX_CONTROL_PACKET_SIZE
#endif

BUILD_ASSERT(EP0_MAX_PACKET_SIZE == 8 || EP0_MAX_PACKET_SIZE == 16 ||
	     EP0_MAX_PACKET_SIZE == 32 || EP0_MAX_PACKET_SIZE == 64);

#define USB_RESUME_TIMEOUT_MS 3000

/* USB Standard Device Descriptor */
static const struct usb_device_descriptor dev_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = USB_DEV_BCDUSB,
	.bDeviceClass = USB_DEV_CLASS,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = EP0_MAX_PACKET_SIZE,
	.idVendor = CONFIG_USB_VID,
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
	.wTotalLength = 0x0BAD, /* no of returned bytes, set at runtime */
	.bNumInterfaces = USB_IFACE_COUNT,
	.bConfigurationValue = 1,
	.iConfiguration = USB_STR_VERSION,
	.bmAttributes = 0x80 /* Reserved bit */
#ifdef CONFIG_USB_SELF_POWERED /* bus or self powered */
			| 0x40
#endif
#ifdef CONFIG_USB_REMOTE_WAKEUP
			| 0x20
#endif
	,
	.bMaxPower = (CONFIG_USB_MAXPOWER_MA / 2),
};

const uint8_t usb_string_desc[] = {
	4, /* Descriptor size */
	USB_DT_STRING, 0x09, 0x04 /* LangID = 0x0409: U.S. English */
};

#ifdef CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR
/*
 * String descriptor for Windows Compatible ID OS Descriptor. This string
 * descriptor is used by Windows OS to know to request a Windows Compatible ID
 * OS Descriptor so that Windows will load the proper WINUSB driver.
 */
const void *const usb_ms_os_string_descriptor = { USB_MS_STRING_DESC(
	"MSFT100") };

/*
 * Extended Compat ID OS Feature descriptor. This descriptor is used by Windows
 * OS to know which type of driver is required so the USB-EP device gets
 * registered properly. This type of descriptor may contain more than one
 * function interface, but this instantiation only uses one function interface
 * to communicate the WINUSB compatible ID.
 */
const struct usb_ms_ext_compat_id_desc winusb_desc = {
	.dwLength = sizeof(struct usb_ms_ext_compat_id_desc),
	.bcdVersion = 0x100, /* Windows Compat ID Desc v1.0 */
	.wIndex = USB_MS_EXT_COMPATIBLE_ID_INDEX,
	.bCount = USB_MS_COMPAT_ID_FUNCTION,
	.function = {
		[0] = {
			.bFirstInterfaceNumber = 0,
			.reserved_1 = 1,
			.compatible_id = {USB_MS_COMPAT_ID}, /* WINUSB */
		},
	},
};
#endif

/* Endpoint table in USB controller RAM */
struct stm32_endpoint btable_ep[USB_EP_COUNT] __aligned(8) __usb_btable;
/* Control endpoint (EP0) buffers */
static usb_uint ep0_buf_tx[EP0_MAX_PACKET_SIZE / 2] __usb_ram;
static usb_uint ep0_buf_rx[EP0_MAX_PACKET_SIZE / 2] __usb_ram;

#define EP0_BUF_TX_SRAM_ADDR ((void *)usb_sram_addr(ep0_buf_tx))

static int set_addr;
/* remaining size of descriptor data to transfer */
static int desc_left;
/* pointer to descriptor data if any */
static const uint8_t *desc_ptr;
/* interface that should handle the next tx transaction */
static uint8_t iface_next = USB_IFACE_COUNT;
#ifdef CONFIG_USB_REMOTE_WAKEUP
/* remote wake up feature enabled */
static int remote_wakeup_enabled;
#endif

void usb_read_setup_packet(usb_uint *buffer, struct usb_setup_packet *packet)
{
	packet->bmRequestType = buffer[0] & 0xff;
	packet->bRequest = buffer[0] >> 8;
	packet->wValue = buffer[1];
	packet->wIndex = buffer[2];
	packet->wLength = buffer[3];
}

struct usb_descriptor_patch {
	const void *address;
	uint16_t data;
};

static struct usb_descriptor_patch desc_patches[USB_DESC_PATCH_COUNT];

void set_descriptor_patch(enum usb_desc_patch_type type, const void *address,
			  uint16_t data)
{
	desc_patches[type].address = address;
	desc_patches[type].data = data;
}

void *memcpy_to_usbram_ep0_patch(const void *src, size_t n)
{
	int i;
	void *ret;

	ret = memcpy_to_usbram((void *)usb_sram_addr(ep0_buf_tx), src, n);

	for (i = 0; i < USB_DESC_PATCH_COUNT; i++) {
		unsigned int offset = desc_patches[i].address - src;

		if (offset >= n)
			continue;

		memcpy_to_usbram((void *)(usb_sram_addr(ep0_buf_tx) + offset),
				 &desc_patches[i].data,
				 sizeof(desc_patches[i].data));
	}

	return ret;
}

static void ep0_send_descriptor(const uint8_t *desc, int len,
				uint16_t fixup_size)
{
	/* do not send more than what the host asked for */
	len = MIN(ep0_buf_rx[3], len);
	/*
	 * if we cannot transmit everything at once,
	 * keep the remainder for the next IN packet
	 */
	if (len >= EP0_MAX_PACKET_SIZE) {
		desc_left = len - EP0_MAX_PACKET_SIZE;
		desc_ptr = desc + EP0_MAX_PACKET_SIZE;
		len = EP0_MAX_PACKET_SIZE;
	}
	memcpy_to_usbram_ep0_patch(desc, len);
	if (fixup_size) /* set the real descriptor size */
		ep0_buf_tx[1] = fixup_size;
	btable_ep[0].tx_count = len;
	/* send the null OUT transaction if the transfer is complete */
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
			desc_left ? 0 : EP_STATUS_OUT);
}

/* Requests on the control endpoint (aka EP0) */
static void ep0_rx(void)
{
	uint16_t req = ep0_buf_rx[0]; /* bRequestType | bRequest */

	/* reset any incomplete descriptor transfer */
	desc_ptr = NULL;
	iface_next = USB_IFACE_COUNT;

	/* interface specific requests */
	if ((req & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		uint8_t iface = ep0_buf_rx[2] & 0xff;
		if (iface < USB_IFACE_COUNT) {
			int ret;

			ret = usb_iface_request[iface](ep0_buf_rx, ep0_buf_tx);
			if (ret < 0)
				goto unknown_req;
			if (ret == 1)
				iface_next = iface;
			return;
		}
	}
	/* vendor specific request */
	if ((req & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
#if defined(CONFIG_WEBUSB_URL) || \
	defined(CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR)
		uint8_t b_req = req >> 8; /* bRequest in the transfer */
		uint16_t w_index = ep0_buf_rx[2]; /* wIndex in the transfer */

#ifdef CONFIG_WEBUSB_URL
		if (b_req == 0x01 && w_index == WEBUSB_REQ_GET_URL) {
			int len = *(uint8_t *)webusb_url;

			ep0_send_descriptor(webusb_url, len, 0);
			return;
		}
#endif /* CONFIG_WEBUSB_URL */

#ifdef CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR
		if (b_req == USB_MS_STRING_DESC_VENDOR_CODE &&
		    w_index == USB_MS_EXT_COMPATIBLE_ID_INDEX) {
			ep0_send_descriptor((uint8_t *)&winusb_desc,
					    winusb_desc.dwLength, 0);
			return;
		}
#endif /* CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR */

#endif /* CONFIG_WEBUSB_URL || CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR */
		goto unknown_req;
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

#ifdef CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR
			/*
			 * String descriptor request at index == 0xEE is used by
			 * Windows OS to know how to retrieve an Extended Compat
			 * ID OS Feature descriptor.
			 */
			if (idx == USB_GET_MS_DESCRIPTOR) {
				desc = (uint8_t *)usb_ms_os_string_descriptor;
				len = desc[0];
				break;
			}
#endif
			if (idx >= USB_STR_COUNT)
				/* The string does not exist : STALL */
				goto unknown_req;
#ifdef CONFIG_USB_SERIALNO
			if (idx == USB_STR_SERIALNO)
				desc = (uint8_t *)usb_serialno_desc;
			else
#endif
				desc = usb_strings[idx];
			len = desc[0];
			break;
		case USB_DT_DEVICE_QUALIFIER: /* Get device qualifier desc */
			/* Not high speed : STALL next IN used as handshake */
			goto unknown_req;
		default: /* unhandled descriptor */
			goto unknown_req;
		}
		ep0_send_descriptor(
			desc, len,
			type == USB_DT_CONFIGURATION ? USB_DESC_SIZE : 0);
	} else if (req == (USB_DIR_IN | (USB_REQ_GET_STATUS << 8))) {
		uint16_t data = 0;
		/* Get status */
#ifdef CONFIG_USB_SELF_POWERED
		data |= USB_REQ_GET_STATUS_SELF_POWERED;
#endif
#ifdef CONFIG_USB_REMOTE_WAKEUP
		if (remote_wakeup_enabled)
			data |= USB_REQ_GET_STATUS_REMOTE_WAKEUP;
#endif
		memcpy_to_usbram(EP0_BUF_TX_SRAM_ADDR, (void *)&data, 2);
		btable_ep[0].tx_count = 2;
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
				EP_STATUS_OUT /*null OUT transaction */);
	} else if ((req & 0xff) == USB_DIR_OUT) {
		switch (req >> 8) {
		case USB_REQ_SET_FEATURE:
		case USB_REQ_CLEAR_FEATURE:
#ifdef CONFIG_USB_REMOTE_WAKEUP
			if (ep0_buf_rx[1] ==
			    USB_REQ_FEATURE_DEVICE_REMOTE_WAKEUP) {
				remote_wakeup_enabled =
					((req >> 8) == USB_REQ_SET_FEATURE);
				btable_ep[0].tx_count = 0;
				STM32_TOGGLE_EP(0, EP_TX_RX_MASK,
						EP_TX_RX_VALID, 0);
				break;
			}
#endif
			goto unknown_req;
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
		int len = MIN(desc_left, EP0_MAX_PACKET_SIZE);
		memcpy_to_usbram(EP0_BUF_TX_SRAM_ADDR, desc_ptr, len);
		btable_ep[0].tx_count = len;
		desc_left -= len;
		desc_ptr += len;
		STM32_TOGGLE_EP(0, EP_TX_MASK, EP_TX_VALID,
				desc_left ? 0 : EP_STATUS_OUT);
		/* send the null OUT transaction if the transfer is complete */
		return;
	}
	if (iface_next < USB_IFACE_COUNT) {
		int ret;

		ret = usb_iface_request[iface_next](NULL, ep0_buf_tx);
		if (ret < 0)
			goto error;
		if (ret == 0)
			iface_next = USB_IFACE_COUNT;
		return;
	}

error:
	STM32_TOGGLE_EP(0, EP_TX_MASK, EP_TX_VALID, 0);
}

static void ep0_event(enum usb_ep_event evt)
{
	if (evt != USB_EVENT_RESET)
		return;

	STM32_USB_EP(0) = BIT(9) /* control EP */ | (2 << 4) /* TX NAK */ |
			  (3 << 12) /* RX VALID */;

	btable_ep[0].tx_addr = usb_sram_addr(ep0_buf_tx);
	btable_ep[0].rx_addr = usb_sram_addr(ep0_buf_rx);
	btable_ep[0].rx_count = usb_ep_rx_size(EP0_MAX_PACKET_SIZE);
	btable_ep[0].tx_count = 0;
}
USB_DECLARE_EP(0, ep0_tx, ep0_rx, ep0_event);

static void usb_reset(void)
{
	int ep;

	for (ep = 0; ep < USB_EP_COUNT; ep++)
		usb_ep_event[ep](USB_EVENT_RESET);

	/*
	 * set the default address : 0
	 * as we are not configured yet
	 */
	STM32_USB_DADDR = 0 | 0x80;
	CPRINTF("RST EP0 %04x\n", STM32_USB_EP(0));
}

#ifdef CONFIG_USB_SUSPEND
static void usb_pm_change_notify_hooks(void)
{
	hook_notify(HOOK_USB_PM_CHANGE);
}
DECLARE_DEFERRED(usb_pm_change_notify_hooks);

/* See RM0091 Reference Manual 30.5.5 Suspend/Resume events */
static void usb_suspend(void)
{
	CPRINTF("SUS%d\n", remote_wakeup_enabled);

	/*
	 * usb_suspend can be called from hook task, make sure no interrupt is
	 * modifying CNTR at the same time.
	 */
	interrupt_disable();
	/* Set FSUSP bit to activate suspend mode */
	STM32_USB_CNTR |= STM32_USB_CNTR_FSUSP;

	/* Set USB low power mode */
	STM32_USB_CNTR |= STM32_USB_CNTR_LP_MODE;
	interrupt_enable();

#if !defined(CHIP_FAMILY_STM32F0)
	clock_enable_module(MODULE_USB, 0);
#endif

	/* USB is not in use anymore, we can (hopefully) sleep now. */
	enable_sleep(SLEEP_MASK_USB_DEVICE);

	hook_call_deferred(&usb_pm_change_notify_hooks_data, 0);
}

/*
 * SOF was received (set in interrupt), reset in usb_resume in the
 * unexpected state case.
 */
static volatile int sof_received;

static void usb_resume_deferred(void)
{
	uint32_t state = (STM32_USB_FNR & STM32_USB_FNR_RXDP_RXDM_MASK) >>
			 STM32_USB_FNR_RXDP_RXDM_SHIFT;

	CPRINTF("RSMd %d %04x %d\n", state, STM32_USB_CNTR, sof_received);
	if (sof_received == 0 && (state == 2 || state == 3))
		usb_suspend();
	else
		hook_call_deferred(&usb_pm_change_notify_hooks_data, 0);
}
DECLARE_DEFERRED(usb_resume_deferred);

static void usb_resume(void)
{
	uint32_t state;

#if !defined(CHIP_FAMILY_STM32F0)
	clock_enable_module(MODULE_USB, 1);
#endif

	/* Clear FSUSP bit to exit suspend mode */
	STM32_USB_CNTR &= ~STM32_USB_CNTR_FSUSP;

	/* USB is in use again */
	disable_sleep(SLEEP_MASK_USB_DEVICE);

	state = (STM32_USB_FNR & STM32_USB_FNR_RXDP_RXDM_MASK) >>
		STM32_USB_FNR_RXDP_RXDM_SHIFT;

	CPRINTF("RSM %d %04x\n", state, STM32_USB_CNTR);

	/*
	 * Reference manual tells we should go back to sleep if state is 10 or
	 * 11. However, setting FSUSP and LP_MODE in this interrupt routine
	 * seems to lock the USB controller (see b/35775088 and b/71688150).
	 * Instead, we do it in a deferred routine. The host must assert the
	 * reset condition for 20ms, so reading D+/D- after ~3ms should be safe
	 * (there is no chance we end up sampling during a bus transaction).
	 */
	if (state == 2 || state == 3) {
		/*
		 * This function is already called from interrupt context so
		 * there is no risk of race here.
		 */
		sof_received = 0;
		STM32_USB_CNTR |= STM32_USB_CNTR_SOFM;
		hook_call_deferred(&usb_resume_deferred_data, 3 * MSEC);
	} else {
		hook_call_deferred(&usb_pm_change_notify_hooks_data, 0);
	}
}

#ifdef CONFIG_USB_REMOTE_WAKEUP
/*
 * Makes sure usb_wake is only run once. When 0, wake is in progress.
 */
static volatile int usb_wake_done = 1;

/*
 * ESOF counter (incremented in interrupt), RESUME bit is cleared when
 * this reaches 0. Also used to detect resume timeout.
 */
static volatile int esof_count;

__attribute__((weak)) void board_usb_wake(void)
{
	/* Side-band USB wake, do nothing by default. */
}

/* Called 10ms after usb_wake started. */
static void usb_wake_deferred(void)
{
	if (esof_count == 3) {
		/*
		 * If we reach here, it means that we are not counting ESOF/SOF
		 * properly (either of these interrupts should occur every 1ms).
		 * This should never happen if we implemented the resume logic
		 * correctly.
		 *
		 * We reset the controller in that case, which recovers the
		 * interface.
		 */
		CPRINTF("USB stuck\n");
#if defined(STM32_RCC_APB1RSTR2_USBFSRST)
		STM32_RCC_APB1RSTR2 |= STM32_RCC_APB1RSTR2_USBFSRST;
		STM32_RCC_APB1RSTR2 &= STM32_RCC_APB1RSTR2_USBFSRST;
#else
		STM32_RCC_APB1RSTR |= STM32_RCC_PB1_USB;
		STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_USB;
#endif
		usb_init();
	}
}
DECLARE_DEFERRED(usb_wake_deferred);

void usb_wake(void)
{
	if (!remote_wakeup_enabled ||
	    !(STM32_USB_CNTR & STM32_USB_CNTR_FSUSP)) {
		/*
		 * USB wake not enabled, or already woken up, or already waking
		 * up, nothing to do.
		 */
		return;
	}

	/* Only allow one caller at a time. */
	if (!atomic_clear((atomic_t *)&usb_wake_done))
		return;

	CPRINTF("WAKE\n");

	/*
	 * Sometimes the USB controller gets stuck, and does not count SOF/ESOF
	 * frames anymore, detect that.
	 */
	hook_call_deferred(&usb_wake_deferred_data, 10 * MSEC);

	/*
	 * Set RESUME bit for 1 to 15 ms, then clear it. We ask the interrupt
	 * routine to count 3 ESOF interrupts, which should take between
	 * 2 and 3 ms.
	 */
	esof_count = 3;

	/* STM32_USB_CNTR can also be updated from interrupt context. */
	interrupt_disable();
	STM32_USB_CNTR |= STM32_USB_CNTR_RESUME | STM32_USB_CNTR_ESOFM |
			  STM32_USB_CNTR_SOFM;
	interrupt_enable();

	/* Try side-band wake as well. */
	board_usb_wake();
}
#endif

int usb_is_suspended(void)
{
	/* Either hardware block is suspended... */
	if (STM32_USB_CNTR & STM32_USB_CNTR_FSUSP)
		return 1;

#ifdef CONFIG_USB_REMOTE_WAKEUP
	/* ... or we are currently waking up. */
	if (!usb_wake_done)
		return 1;
#endif

	return 0;
}

int usb_is_remote_wakeup_enabled(void)
{
#ifdef CONFIG_USB_REMOTE_WAKEUP
	return remote_wakeup_enabled;
#else
	return 0;
#endif
}
#endif /* CONFIG_USB_SUSPEND */

#if defined(CONFIG_USB_SUSPEND) && defined(CONFIG_USB_REMOTE_WAKEUP)
/*
 * Called by usb_interrupt when usb_wake is asking us to count esof_count ESOF
 * interrupts (one per millisecond), then disable RESUME, then wait for resume
 * to complete.
 */
static void usb_interrupt_handle_wake(uint16_t status)
{
	int state;
	int good;

	esof_count--;

	/* Keep counting. */
	if (esof_count > 0)
		return;

	/* Clear RESUME bit. */
	if (esof_count == 0)
		STM32_USB_CNTR &= ~STM32_USB_CNTR_RESUME;

	/* Then count down until state is resumed. */
	state = (STM32_USB_FNR & STM32_USB_FNR_RXDP_RXDM_MASK) >>
		STM32_USB_FNR_RXDP_RXDM_SHIFT;

	/*
	 * state 2, or receiving an SOF, means resume
	 * completed successfully.
	 */
	good = (status & STM32_USB_ISTR_SOF) || (state == 2);

	/* Either: state is ready, or we timed out. */
	if (good || state == 3 || esof_count <= -USB_RESUME_TIMEOUT_MS) {
		int ep;

		STM32_USB_CNTR &= ~STM32_USB_CNTR_ESOFM;
		usb_wake_done = 1;
		if (!good) {
			CPRINTF("wake error: cnt=%d state=%d\n", esof_count,
				state);
			usb_suspend();
			return;
		}

		CPRINTF("RSMOK%d %d\n", -esof_count, state);

		for (ep = 1; ep < USB_EP_COUNT; ep++)
			usb_ep_event[ep](USB_EVENT_DEVICE_RESUME);
	}
}
#endif /* CONFIG_USB_SUSPEND && CONFIG_USB_REMOTE_WAKEUP */

static void usb_interrupt(void)
{
	uint16_t status = STM32_USB_ISTR;

	if (status & STM32_USB_ISTR_RESET)
		usb_reset();

#ifdef CONFIG_USB_SUSPEND
	if (status & STM32_USB_ISTR_SOF) {
		sof_received = 1;
		/*
		 * The wake handler also only cares about the _first_ SOF that
		 * is received, so we can disable that interrupt.
		 */
		STM32_USB_CNTR &= ~STM32_USB_CNTR_SOFM;
	}

#ifdef CONFIG_USB_REMOTE_WAKEUP
	if (status & (STM32_USB_ISTR_ESOF | STM32_USB_ISTR_SOF) &&
	    !usb_wake_done)
		usb_interrupt_handle_wake(status);
#endif

	if (status & STM32_USB_ISTR_SUSP)
		usb_suspend();

	if (status & STM32_USB_ISTR_WKUP)
		usb_resume();
#endif

	if (status & STM32_USB_ISTR_CTR) {
		int ep = status & STM32_USB_ISTR_EP_ID_MASK;
		if (ep < USB_EP_COUNT) {
			if (status & STM32_USB_ISTR_DIR)
				usb_ep_rx[ep]();
			else
				usb_ep_tx[ep]();
		}
		/* TODO: do it in a USB task */
		/* task_set_event(, 1 << ep_task); */
	}

	/* ack only interrupts that we handled */
	STM32_USB_ISTR = ~status;
}
DECLARE_IRQ(STM32_IRQ_USB_LP, usb_interrupt, 1);

void usb_init(void)
{
	/* Enable USB device clock, possibly increasing system clock to 48MHz */
	clock_enable_module(MODULE_USB, 1);

	/* configure the pinmux */
	gpio_config_module(MODULE_USB, 1);

	/* power on sequence */

	/* keep FRES (USB reset) and remove PDWN (power down) */
	STM32_USB_CNTR = STM32_USB_CNTR_FRES;
	udelay(1); /* startup time */
	/* reset FRES and keep interrupts masked */
	STM32_USB_CNTR = 0x00;
	/* clear pending interrupts */
	STM32_USB_ISTR = 0;

	/* set descriptors table offset in dedicated SRAM */
	STM32_USB_BTABLE = 0;

	/* EXTI18 is USB wake up interrupt */
	/* STM32_EXTI_RTSR |= BIT(18); */
	/* STM32_EXTI_IMR |= BIT(18); */

	/* Enable interrupt handlers */
	task_enable_irq(STM32_IRQ_USB_LP);
	/* set interrupts mask : reset/correct transfer/errors */
	STM32_USB_CNTR = STM32_USB_CNTR_CTRM | STM32_USB_CNTR_PMAOVRM |
			 STM32_USB_CNTR_ERRM |
#ifdef CONFIG_USB_SUSPEND
			 STM32_USB_CNTR_WKUPM | STM32_USB_CNTR_SUSPM |
#endif
			 STM32_USB_CNTR_RESETM;

#ifdef CONFIG_USB_SERIALNO
	usb_load_serial();
#endif
#ifndef CONFIG_USB_INHIBIT_CONNECT
	usb_connect();
#endif

	CPRINTF("USB init done\n");
}

#ifndef CONFIG_USB_INHIBIT_INIT
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);
#endif

void usb_release(void)
{
	/* signal disconnect to host */
	usb_disconnect();

	/* power down USB */
	STM32_USB_CNTR = 0;

	/* disable interrupt handlers */
	task_disable_irq(STM32_IRQ_USB_LP);

	/* unset pinmux */
	gpio_config_module(MODULE_USB, 0);

	/* disable USB device clock, possibly slowing down system clock */
	clock_enable_module(MODULE_USB, 0);
}
/* ensure the host disconnects and reconnects over a sysjump */
DECLARE_HOOK(HOOK_SYSJUMP, usb_release, HOOK_PRIO_DEFAULT);

int usb_is_enabled(void)
{
	return clock_is_module_enabled(MODULE_USB);
}

void *memcpy_to_usbram(void *dest, const void *src, size_t n)
{
	int unaligned = (((uintptr_t)dest) & 1);
	usb_uint *d = &__usb_ram_start[((uintptr_t)dest) / 2];
	uint8_t *s = (uint8_t *)src;
	int i;

	/*
	 * Handle unaligned leading byte via read/modify/write.
	 */
	if (unaligned && n) {
		*d = (*d & ~0xff00) | (*s << 8);
		n--;
		s++;
		d++;
	}

	for (i = 0; i < n / 2; i++, s += 2)
		*d++ = (s[1] << 8) | s[0];

	/*
	 * There is a trailing byte to write into a final USB packet memory
	 * location, use a read/modify/write to be safe.
	 */
	if (n & 1)
		*d = (*d & ~0x00ff) | *s;

	return dest;
}

void *memcpy_from_usbram(void *dest, const void *src, size_t n)
{
	int unaligned = (((uintptr_t)src) & 1);
	usb_uint const *s = &__usb_ram_start[((uintptr_t)src) / 2];
	uint8_t *d = (uint8_t *)dest;
	int i;

	if (unaligned && n) {
		*d = *s >> 8;
		n--;
		s++;
		d++;
	}

	for (i = 0; i < n / 2; i++) {
		usb_uint value = *s++;

		*d++ = (value >> 0) & 0xff;
		*d++ = (value >> 8) & 0xff;
	}

	if (n & 1)
		*d = *s;

	return dest;
}

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

static int command_serialno(int argc, const char **argv)
{
	struct usb_string_desc *sd = usb_serialno_desc;
	char buf[CONFIG_SERIALNO_LEN];
	int rv = EC_SUCCESS;
	int i;

	if (argc != 1) {
		if ((strcasecmp(argv[1], "set") == 0) && (argc == 3)) {
			ccprintf("Saving serial number\n");
			rv = usb_save_serial(argv[2]);
		} else if ((strcasecmp(argv[1], "load") == 0) && (argc == 2)) {
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

DECLARE_CONSOLE_COMMAND(serialno, command_serialno, "load/set [value]",
			"Read and write USB serial number");

#endif /* CONFIG_USB_SERIALNO */

#ifdef CONFIG_MAC_ADDR

/* Save MAC address into pstate region. */
static int usb_save_mac_addr(const char *mac_addr)
{
	int rv;

	if (!mac_addr) {
		return EC_ERROR_INVAL;
	}

	/* Save this new MAC address to flash. */
	rv = board_write_mac_addr(mac_addr);
	if (rv) {
		return rv;
	}

	/* Load this new MAC address to memory. */
	if (board_read_mac_addr() != NULL) {
		return EC_SUCCESS;
	} else {
		return EC_ERROR_UNKNOWN;
	}
}

static int command_macaddr(int argc, const char **argv)
{
	const char *buf;
	int rv = EC_SUCCESS;

	if (argc != 1) {
		if ((strcasecmp(argv[1], "set") == 0) && (argc == 3)) {
			ccprintf("Saving MAC address\n");
			rv = usb_save_mac_addr(argv[2]);
		} else if ((strcasecmp(argv[1], "load") == 0) && (argc == 2)) {
			ccprintf("Loading MAC address\n");
		} else {
			return EC_ERROR_INVAL;
		}
	}

	buf = board_read_mac_addr();
	if (buf == NULL) {
		buf = DEFAULT_MAC_ADDR;
	}
	ccprintf("MAC address: %s\n", buf);
	return rv;
}

DECLARE_CONSOLE_COMMAND(macaddr, command_macaddr, "load/set [value]",
			"Read and write MAC address");

#endif /* CONFIG_MAC_ADDR */
