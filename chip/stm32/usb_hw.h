/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_HW_H
#define __CROS_EC_USB_HW_H

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
#define __usb_ram __attribute__((section(".usb_ram.data")))

struct stm32_endpoint {
	volatile usb_uint tx_addr;
	volatile usb_uint tx_count;
	volatile usb_uint rx_addr;
	volatile usb_uint rx_count;
};

extern struct stm32_endpoint btable_ep[];

/* Attribute to put the endpoint table in USB RAM */
#define __usb_btable __attribute__((section(".usb_ram.btable")))

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

/* Compute the address inside dedicate SRAM for the USB controller */
#define usb_sram_addr(x) ((x - __usb_ram_start) * sizeof(uint16_t))

/* Helpers for endpoint declaration */
#define _EP_HANDLER2(num, suffix) CONCAT3(ep_, num, suffix)
#define _EP_TX_HANDLER(num) _EP_HANDLER2(num, _tx)
#define _EP_RX_HANDLER(num) _EP_HANDLER2(num, _rx)
#define _EP_RESET_HANDLER(num) _EP_HANDLER2(num, _rst)

#define USB_DECLARE_EP(num, tx_handler, rx_handler, rst_handler)  \
	void _EP_TX_HANDLER(num)(void)				  \
		__attribute__ ((alias(STRINGIFY(tx_handler))));	  \
	void _EP_RX_HANDLER(num)(void)                            \
		__attribute__ ((alias(STRINGIFY(rx_handler))));	  \
	void _EP_RESET_HANDLER(num)(void)                         \
		__attribute__ ((alias(STRINGIFY(rst_handler))));

/* arrays with all endpoint callbacks */
extern void (*usb_ep_tx[]) (void);
extern void (*usb_ep_rx[]) (void);
extern void (*usb_ep_reset[]) (void);
/* array with interface-specific control request callbacks */
extern int (*usb_iface_request[]) (usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx);

/*
 * Interface handler returns -1 on error, 0 if it wrote the last chunk of data,
 * or 1 if more data needs to be transferred on the next control request.
 */
#define _IFACE_HANDLER(num) CONCAT3(iface_, num, _request)
#define USB_DECLARE_IFACE(num, handler)					\
	int _IFACE_HANDLER(num)(usb_uint *ep0_buf_rx,			\
			       usb_uint *epo_buf_tx)			\
	__attribute__ ((alias(STRINGIFY(handler))));

#endif
#endif	/* __CROS_EC_USB_HW_H */
