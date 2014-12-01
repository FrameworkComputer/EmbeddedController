/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB definitions.
 */

#ifndef USB_H
#define USB_H

#include <stddef.h> /* for wchar_t */

#include "usb_api.h"

#define USB_MAX_PACKET_SIZE 64

/* USB 2.0 chapter 9 definitions */

/* Descriptor types */
#define USB_DT_DEVICE                     0x01
#define USB_DT_CONFIGURATION              0x02
#define USB_DT_STRING                     0x03
#define USB_DT_INTERFACE                  0x04
#define USB_DT_ENDPOINT                   0x05
#define USB_DT_DEVICE_QUALIFIER           0x06
#define USB_DT_OTHER_SPEED_CONFIG         0x07
#define USB_DT_INTERFACE_POWER            0x08
#define USB_DT_DEBUG                      0x0a
#define USB_DT_BOS                        0x0f
#define USB_DT_DEVICE_CAPABILITY          0x10

/* USB Device Descriptor */
struct usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} __packed;
#define USB_DT_DEVICE_SIZE                18

/* BOS Descriptor ( USB3.1 rev1 Section 9.6.2 ) */
struct bos_context {
	void *descp;
	int size;
};

struct usb_bos_hdr_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType; /* USB_DT_BOS */
	uint16_t wTotalLength;   /* Total length of of hdr + all dev caps */
	uint8_t bNumDeviceCaps;  /* Container ID Descriptor + others */
} __packed;
#define USB_DT_BOS_SIZE 5

/* Container ID Descriptor */
struct usb_contid_caps_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;     /* USB_DT_DEVICE_CAPABILITY */
	uint8_t  bDevCapabilityType;  /* USB_DC_DTYPE_xxx */
	uint8_t  bReserved;           /* SBZ */
	uint8_t  ContainerID[16];     /* UUID */
} __packed;
#define USB_DT_CONTID_SIZE         20

/* Device Cap Type Codes ( offset 2 of Device Capability Descriptor */
#define USB_DC_DTYPE_WIRELESS  0x01
#define USB_DC_DTYPE_USB20EXT  0x02
#define USB_DC_DTYPE_USBSS     0x03
#define USB_DC_DTYPE_CONTID    0x04
#define USB_DC_DTYPE_PLATFORM  0x05
#define USB_DC_DTYPE_PD        0x06
#define USB_DC_DTYPE_BATTINFO  0x07
#define USB_DC_DTYPE_CONSUMER  0x08
#define USB_DC_DTYPE_PRODUCER  0x09
#define USB_DC_DTYPE_USBSSP    0x0a
#define USB_DC_DTYPE_PCSTIME   0x0b
#define USB_DC_DTYPE_WUSBEXT   0x0c
#define USB_DC_DTYPE_BILLBOARD 0x0d
/* RESERVED 0x00, 0xOe - 0xff */

/* Configuration Descriptor */
struct usb_config_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wTotalLength;
	uint8_t  bNumInterfaces;
	uint8_t  bConfigurationValue;
	uint8_t  iConfiguration;
	uint8_t  bmAttributes;
	uint8_t  bMaxPower;
} __packed;
#define USB_DT_CONFIG_SIZE                9

/* String Descriptor */
struct usb_string_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wData[1];
} __packed;

/* Interface Descriptor */
struct usb_interface_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bInterfaceNumber;
	uint8_t  bAlternateSetting;
	uint8_t  bNumEndpoints;
	uint8_t  bInterfaceClass;
	uint8_t  bInterfaceSubClass;
	uint8_t  bInterfaceProtocol;
	uint8_t  iInterface;
} __packed;
#define USB_DT_INTERFACE_SIZE           9

/* Endpoint Descriptor */
struct usb_endpoint_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bEndpointAddress;
	uint8_t  bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t  bInterval;
} __packed;
#define USB_DT_ENDPOINT_SIZE            7

/* USB Class codes */
#define USB_CLASS_PER_INTERFACE           0x00
#define USB_CLASS_AUDIO                   0x01
#define USB_CLASS_COMM                    0x02
#define USB_CLASS_HID                     0x03
#define USB_CLASS_PHYSICAL                0x05
#define USB_CLASS_STILL_IMAGE             0x06
#define USB_CLASS_PRINTER                 0x07
#define USB_CLASS_MASS_STORAGE            0x08
#define USB_CLASS_HUB                     0x09
#define USB_CLASS_CDC_DATA                0x0a
#define USB_CLASS_CSCID                   0x0b
#define USB_CLASS_CONTENT_SEC             0x0d
#define USB_CLASS_VIDEO                   0x0e
#define USB_CLASS_BILLBOARD               0x11
#define USB_CLASS_WIRELESS_CONTROLLER     0xe0
#define USB_CLASS_MISC                    0xef
#define USB_CLASS_APP_SPEC                0xfe
#define USB_CLASS_VENDOR_SPEC             0xff

/* USB Vendor ID assigned to Google Inc. */
#define USB_VID_GOOGLE 0x18d1

/* Google specific SubClass/Protocol assignments */
#define USB_SUBCLASS_GOOGLE_SERIAL 0x50
#define USB_PROTOCOL_GOOGLE_SERIAL 0x01

/* Control requests */

/* bRequestType fields */
/* direction field */
#define USB_DIR_OUT                0    /* from host to uC */
#define USB_DIR_IN                 0x80 /* from uC to host */
/* type field */
#define USB_TYPE_MASK              (0x03 << 5)
#define USB_TYPE_STANDARD          (0x00 << 5)
#define USB_TYPE_CLASS             (0x01 << 5)
#define USB_TYPE_VENDOR            (0x02 << 5)
#define USB_TYPE_RESERVED          (0x03 << 5)
/* recipient field */
#define USB_RECIP_MASK             0x1f
#define USB_RECIP_DEVICE           0x00
#define USB_RECIP_INTERFACE        0x01
#define USB_RECIP_ENDPOINT         0x02
#define USB_RECIP_OTHER            0x03

/* Standard requests for bRequest field in a SETUP packet. */
#define USB_REQ_GET_STATUS         0x00
#define USB_REQ_CLEAR_FEATURE      0x01
#define USB_REQ_SET_FEATURE        0x03
#define USB_REQ_SET_ADDRESS        0x05
#define USB_REQ_GET_DESCRIPTOR     0x06
#define USB_REQ_SET_DESCRIPTOR     0x07
#define USB_REQ_GET_CONFIGURATION  0x08
#define USB_REQ_SET_CONFIGURATION  0x09
#define USB_REQ_GET_INTERFACE      0x0A
#define USB_REQ_SET_INTERFACE      0x0B
#define USB_REQ_SYNCH_FRAME        0x0C

#define USB_BC12_CHARGE_VOLTAGE    5000 /* mV */

/* Helpers for descriptors */

#define WIDESTR(quote) WIDESTR2(quote)
#define WIDESTR2(quote) L##quote

#define USB_STRING_DESC(str) \
	(const void *)&(const struct { \
		uint8_t _len; \
		uint8_t _type; \
		wchar_t _data[sizeof(str)]; \
	}) { \
		/* Total size of the descriptor is : \
		 * size of the UTF-16 text plus the len/type fields \
                 * minus the string 0-termination \
                 */ \
		sizeof(WIDESTR(str)) + 2 - 2, \
		USB_DT_STRING, \
		WIDESTR(str) \
	}


#define USB_CONF_DESC(name) CONCAT2(usb_desc_, name) \
	 __attribute__((section(".rodata.usb_desc_" STRINGIFY(name))))

/* build descriptor names to order them properly */
#define USB_IFACE_DESC(num) USB_CONF_DESC(CONCAT3(iface, num, _0iface))
#define USB_EP_DESC(i, num) USB_CONF_DESC(CONCAT4(iface, i, _1ep, num))
#define USB_CUSTOM_DESC(i, name) USB_CONF_DESC(CONCAT4(iface, i, _2, name))

#define USB_DESC_SIZE (__usb_desc_end - __usb_desc)

/* Helpers for managing the USB controller dedicated RAM */

/* primitive to access the words in USB RAM */
typedef CONFIG_USB_RAM_ACCESS_TYPE usb_uint;

struct stm32_endpoint {
	volatile usb_uint tx_addr;
	volatile usb_uint tx_count;
	volatile usb_uint rx_addr;
	volatile usb_uint rx_count;
};

/* attribute to define a variable in USB RAM */
#define __usb_ram __attribute__((section(".usb_ram.data")))

extern struct stm32_endpoint btable_ep[];

/* Copy data to the USB dedicated RAM and take care of the weird addressing */
static inline void memcpy_usbram(usb_uint *ebuf, const uint8_t *src, int size)
{
	int i;
	for (i = 0; i < size / 2; i++, src += 2)
		*ebuf++ = src[0] | (src[1] << 8);
	if (size & 1)
		*ebuf++ = src[0];
}

/* Compute the address inside dedicate SRAM for the USB controller */
#define usb_sram_addr(x) \
	(((uint32_t)(x) - (uint32_t)__usb_ram_start) \
	/ (sizeof(usb_uint)/sizeof(uint16_t)))

/* These descriptors defined in board code */
extern const void * const usb_strings[];
extern const uint8_t usb_string_desc[];
/* USB string descriptor with the firmware version */
extern const void * const usb_fw_version;
extern const struct bos_context bos_ctx;

/* Helpers for endpoint declaration */

#define EP_HANDLER2(num, suffix) CONCAT3(ep_, num, suffix)
#define EP_TX_HANDLER(num) EP_HANDLER2(num, _tx)
#define EP_RX_HANDLER(num) EP_HANDLER2(num, _rx)
#define EP_RESET_HANDLER(num) EP_HANDLER2(num, _rst)

#define USB_DECLARE_EP(num, tx_handler, rx_handler, rst_handler) \
	void EP_TX_HANDLER(num)(void)                            \
		__attribute__ ((alias(STRINGIFY(tx_handler))));  \
	void EP_RX_HANDLER(num)(void)                            \
		__attribute__ ((alias(STRINGIFY(rx_handler))));  \
	void EP_RESET_HANDLER(num)(void)                         \
		__attribute__ ((alias(STRINGIFY(rst_handler))));

/* arrays with all endpoint callbacks */
extern void (*usb_ep_tx[]) (void);
extern void (*usb_ep_rx[]) (void);
extern void (*usb_ep_reset[]) (void);
/* array with interface-specific control request callbacks */
extern int (*usb_iface_request[]) (usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx);

#define IFACE_HANDLER(num) CONCAT3(iface_, num, _request)
#define USB_DECLARE_IFACE(num, handler)					\
	int IFACE_HANDLER(num)(usb_uint *ep0_buf_rx,			\
			       usb_uint *epo_buf_tx)			\
	__attribute__ ((alias(STRINGIFY(handler))));

#endif /* USB_H */
