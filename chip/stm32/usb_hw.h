/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_HW_H
#define __CROS_EC_USB_HW_H

#include <stddef.h>
#include <stdint.h>

/* Event types for the endpoint event handler. */
enum usb_ep_event {
	USB_EVENT_RESET,
	USB_EVENT_DEVICE_RESUME, /* Device-initiated wake completed. */
};

#if defined(CHIP_FAMILY_STM32F4)
#include "usb_dwc_hw.h"
#else

/*
 * The STM32 has dedicated USB RAM visible on the APB1 bus (so all reads &
 * writes are 16-bits wide). The endpoint tables and the data buffers live in
 * this RAM.
 */

/* Primitive to access the words in USB RAM */
typedef CONFIG_USB_RAM_ACCESS_TYPE usb_uint;
/* Linker symbol for start of USB RAM */
extern usb_uint __usb_ram_start[];

/* Attribute to define a buffer variable in USB RAM */
#define __usb_ram __attribute__((section(".usb_ram.99_data")))

/* Mask for the rx_count to identify the number of bytes in the buffer. */
#define RX_COUNT_MASK (0x3ff)

struct stm32_endpoint {
	volatile usb_uint tx_addr;
	volatile usb_uint tx_count;
	volatile usb_uint rx_addr;
	volatile usb_uint rx_count;
};

extern struct stm32_endpoint btable_ep[];

/* Attribute to put the endpoint table in USB RAM */
#define __usb_btable __attribute__((section(".usb_ram.00_btable")))

/* Read from USB RAM into a usb_setup_packet struct */
struct usb_setup_packet;
void usb_read_setup_packet(usb_uint *buffer, struct usb_setup_packet *packet);

/*
 * Copy data to and from the USB dedicated RAM and take care of the weird
 * addressing.  These functions correctly handle unaligned accesses to the USB
 * memory.  They have the same prototype as memcpy, allowing them to be used
 * in places that expect memcpy.  The void pointer used to represent a location
 * in the USB dedicated RAM should be the offset in that address space, not the
 * AHB address space.
 *
 * The USB packet RAM is attached to the processor via the AHB2APB bridge.  This
 * bridge performs manipulations of read and write accesses as per the note in
 * section 2.1 of RM0091.  The upshot is that custom memcpy-like routines need
 * to be employed.
 */
void *memcpy_to_usbram(void *dest, const void *src, size_t n);
void *memcpy_from_usbram(void *dest, const void *src, size_t n);

/*
 * Descriptor patching support, useful to change a few values in the descriptor
 * (typically, length or bitfields) without having to move descriptors to RAM.
 */

enum usb_desc_patch_type {
#ifdef CONFIG_USB_HID_KEYBOARD_BACKLIGHT
	USB_DESC_KEYBOARD_BACKLIGHT,
#endif
	USB_DESC_PATCH_COUNT,
};

/*
 * Set patch in table: replace uint16_t at address (STM32 flash) with data.
 *
 * The patches need to be setup before _before_ usb_init is executed (or, at
 * least, before the first call to memcpy_to_usbram_ep0_patch).
 */
void set_descriptor_patch(enum usb_desc_patch_type type, const void *address,
			  uint16_t data);

/* Copy to USB ram, applying patches to src as required. */
void *memcpy_to_usbram_ep0_patch(const void *src, size_t n);

/* Compute the address inside dedicate SRAM for the USB controller */
#define usb_sram_addr(x) ((x - __usb_ram_start) * sizeof(uint16_t))

/* Compute value to put into rx_count */
#define usb_ep_rx_size(x) ((x) < 64 ? (x) << 9 : 0x8000 | (((x)-32) << 5))

/* Helpers for endpoint declaration */
#define _EP_HANDLER2(num, suffix) CONCAT3(ep_, num, suffix)
#define _EP_TX_HANDLER(num) _EP_HANDLER2(num, _tx)
#define _EP_RX_HANDLER(num) _EP_HANDLER2(num, _rx)
#define _EP_EVENT_HANDLER(num) _EP_HANDLER2(num, _evt)
/* Used to check function types are correct (attribute alias does not do it) */
#define _EP_TX_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _tx_typecheck)
#define _EP_RX_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _rx_typecheck)
#define _EP_EVENT_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _evt_typecheck)

#define USB_DECLARE_EP(num, tx_handler, rx_handler, evt_handler)      \
	void _EP_TX_HANDLER(num)(void)                                \
		__attribute__((alias(STRINGIFY(tx_handler))));        \
	void _EP_RX_HANDLER(num)(void)                                \
		__attribute__((alias(STRINGIFY(rx_handler))));        \
	void _EP_EVENT_HANDLER(num)(enum usb_ep_event evt)            \
		__attribute__((alias(STRINGIFY(evt_handler))));       \
	static __unused void (*_EP_TX_HANDLER_TYPECHECK(num))(void) = \
		tx_handler;                                           \
	static __unused void (*_EP_RX_HANDLER_TYPECHECK(num))(void) = \
		rx_handler;                                           \
	static __unused void (*_EP_EVENT_HANDLER_TYPECHECK(num))(     \
		enum usb_ep_event evt) = evt_handler

/* arrays with all endpoint callbacks */
extern void (*usb_ep_tx[])(void);
extern void (*usb_ep_rx[])(void);
extern void (*usb_ep_event[])(enum usb_ep_event evt);
/* array with interface-specific control request callbacks */
extern int (*usb_iface_request[])(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx);

/*
 * Interface handler returns -1 on error, 0 if it wrote the last chunk of data,
 * or 1 if more data needs to be transferred on the next control request.
 */
#define _IFACE_HANDLER(num) CONCAT3(iface_, num, _request)
#define USB_DECLARE_IFACE(num, handler)                                       \
	int _IFACE_HANDLER(num)(usb_uint * ep0_buf_rx, usb_uint * epo_buf_tx) \
		__attribute__((alias(STRINGIFY(handler))));

#endif

/*
 * In and out buffer sizes for host command over USB.
 */
#define USBHC_MAX_REQUEST_SIZE 0x200
#define USBHC_MAX_RESPONSE_SIZE 0x100

#endif /* __CROS_EC_USB_HW_H */
