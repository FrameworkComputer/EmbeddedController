/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB definitions.
 */

#ifndef __CROS_EC_USB_DESCRIPTOR_H
#define __CROS_EC_USB_DESCRIPTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_MAX_PACKET_SIZE 64

/* USB 2.0 chapter 9 definitions */

/* Descriptor types */
#define USB_DT_DEVICE 0x01
#define USB_DT_CONFIGURATION 0x02
#define USB_DT_STRING 0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT 0x05
#define USB_DT_DEVICE_QUALIFIER 0x06
#define USB_DT_OTHER_SPEED_CONFIG 0x07
#define USB_DT_INTERFACE_POWER 0x08
#define USB_DT_DEBUG 0x0a
#define USB_DT_BOS 0x0f
#define USB_DT_DEVICE_CAPABILITY 0x10

#ifndef CONFIG_ZEPHYR
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

/* Setup Packet */
struct usb_setup_packet {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

/* String Descriptor */
struct usb_string_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wData[1];
} __packed;
#endif /* CONFIG_ZEPHYR */

#define USB_DT_DEVICE_SIZE 18

/* BOS Descriptor ( USB3.1 rev1 Section 9.6.2 ) */
struct bos_context {
	void *descp;
	int size;
};

struct usb_bos_hdr_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType; /* USB_DT_BOS */
	uint16_t wTotalLength; /* Total length of of hdr + all dev caps */
	uint8_t bNumDeviceCaps; /* Container ID Descriptor + others */
} __packed;
#define USB_DT_BOS_SIZE 5

/* Container ID Descriptor */
struct usb_contid_caps_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType; /* USB_DT_DEVICE_CAPABILITY */
	uint8_t bDevCapabilityType; /* USB_DC_DTYPE_xxx */
	uint8_t bReserved; /* SBZ */
	uint8_t ContainerID[16]; /* UUID */
} __packed;
#define USB_DT_CONTID_SIZE 20

/* Device Cap Type Codes ( offset 2 of Device Capability Descriptor */
#define USB_DC_DTYPE_WIRELESS 0x01
#define USB_DC_DTYPE_USB20EXT 0x02
#define USB_DC_DTYPE_USBSS 0x03
#define USB_DC_DTYPE_CONTID 0x04
#define USB_DC_DTYPE_PLATFORM 0x05
#define USB_DC_DTYPE_PD 0x06
#define USB_DC_DTYPE_BATTINFO 0x07
#define USB_DC_DTYPE_CONSUMER 0x08
#define USB_DC_DTYPE_PRODUCER 0x09
#define USB_DC_DTYPE_USBSSP 0x0a
#define USB_DC_DTYPE_PCSTIME 0x0b
#define USB_DC_DTYPE_WUSBEXT 0x0c
#define USB_DC_DTYPE_BILLBOARD 0x0d
/* RESERVED 0x00, 0xOe - 0xff */

/* Platform descriptor */
struct usb_platform_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType; /* USB_DT_DEVICE_CAPABILITY */
	uint8_t bDevCapabilityType; /* USB_DC_DTYPE_PLATFORM */
	uint8_t bReserved; /* SBZ */
	uint8_t PlatformCapUUID[16]; /* USB_PLAT_CAP_xxx */
	uint16_t bcdVersion; /* 0x0100 */
	uint8_t bVendorCode;
	uint8_t iLandingPage;
} __packed;
#define USB_DT_PLATFORM_SIZE 24

/* Platform Capability UUIDs */
#define USB_PLAT_CAP_WEBUSB /*{3408b638-09a9-47a0-8bfd-a0768815b665}*/      \
	{                                                                   \
		0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47, 0x8B, 0xFD, \
			0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65                  \
	}

/* Qualifier Descriptor */
struct usb_qualifier_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint8_t bNumConfigurations;
	uint8_t bReserved;
} __packed;
#define USB_DT_QUALIFIER_SIZE 10

/* Configuration Descriptor */
struct usb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
} __packed;
#define USB_DT_CONFIG_SIZE 9

/* Interface Descriptor */
struct usb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} __packed;
#define USB_DT_INTERFACE_SIZE 9

/* Endpoint Descriptor */
struct usb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} __packed;
#define USB_DT_ENDPOINT_SIZE 7

/* USB Class codes */
#define USB_CLASS_PER_INTERFACE 0x00
#define USB_CLASS_AUDIO 0x01
#define USB_CLASS_COMM 0x02
#define USB_CLASS_HID 0x03
#define USB_CLASS_PHYSICAL 0x05
#define USB_CLASS_STILL_IMAGE 0x06
#define USB_CLASS_PRINTER 0x07
#define USB_CLASS_MASS_STORAGE 0x08
#define USB_CLASS_HUB 0x09
#define USB_CLASS_CDC_DATA 0x0a
#define USB_CLASS_CSCID 0x0b
#define USB_CLASS_CONTENT_SEC 0x0d
#define USB_CLASS_VIDEO 0x0e
#define USB_CLASS_BILLBOARD 0x11
#define USB_CLASS_WIRELESS_CONTROLLER 0xe0
#define USB_CLASS_MISC 0xef
#define USB_CLASS_APP_SPEC 0xfe
#define USB_CLASS_VENDOR_SPEC 0xff

/* USB Vendor ID assigned to Google LLC */
#define USB_VID_GOOGLE 0x18d1

/* Google specific SubClass/Protocol assignments */
#define USB_SUBCLASS_GOOGLE_SERIAL 0x50
#define USB_PROTOCOL_GOOGLE_SERIAL 0x01

#define USB_SUBCLASS_GOOGLE_SPI 0x51
#define USB_PROTOCOL_GOOGLE_SPI 0x02

#define USB_SUBCLASS_GOOGLE_I2C 0x52
#define USB_PROTOCOL_GOOGLE_I2C 0x01

#define USB_SUBCLASS_GOOGLE_UPDATE 0x53
#define USB_PROTOCOL_GOOGLE_UPDATE 0xff

/* Double define for cr50 code freeze.
 * TODO(vbendeb): dedupe this. */
#define USB_SUBCLASS_GOOGLE_CR50 0x53
/* We can use any protocol we want */
#define USB_PROTOCOL_GOOGLE_CR50_NON_HC_FW_UPDATE 0xff

#define USB_SUBCLASS_GOOGLE_POWER 0x54
#define USB_PROTOCOL_GOOGLE_POWER 0x01

#define USB_SUBCLASS_GOOGLE_HEATMAP 0x55
#define USB_PROTOCOL_GOOGLE_HEATMAP 0x01

#define USB_SUBCLASS_GOOGLE_HOSTCMD 0x56
#define USB_PROTOCOL_GOOGLE_HOSTCMD 0xff

/* Control requests */

/* bRequestType fields */
/* direction field */
#define USB_DIR_OUT 0 /* from host to uC */
#define USB_DIR_IN 0x80 /* from uC to host */
/* type field */
#define USB_TYPE_MASK (0x03 << 5)
#define USB_TYPE_STANDARD (0x00 << 5)
#define USB_TYPE_CLASS (0x01 << 5)
#define USB_TYPE_VENDOR (0x02 << 5)
#define USB_TYPE_RESERVED (0x03 << 5)
/* recipient field */
#define USB_RECIP_MASK 0x1f
#define USB_RECIP_DEVICE 0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT 0x02
#define USB_RECIP_OTHER 0x03

/* Standard requests for bRequest field in a SETUP packet. */
#define USB_REQ_GET_STATUS 0x00
#define USB_REQ_GET_STATUS_SELF_POWERED BIT(0)
#define USB_REQ_GET_STATUS_REMOTE_WAKEUP BIT(1)
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_FEATURE_ENDPOINT_HALT 0x0000
#define USB_REQ_FEATURE_DEVICE_REMOTE_WAKEUP 0x0001
#define USB_REQ_FEATURE_TEST_MODE 0x0002
#define USB_REQ_SET_ADDRESS 0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE 0x0A
#define USB_REQ_SET_INTERFACE 0x0B
#define USB_REQ_SYNCH_FRAME 0x0C

/* WebUSB URL descriptors */
#define WEBUSB_REQ_GET_URL 0x02
#define USB_DT_WEBUSB_URL 0x03

#define USB_URL_SCHEME_HTTP 0x00
#define USB_URL_SCHEME_HTTPS 0x01
#define USB_URL_SCHEME_NONE 0xff

/*
 * URL descriptor helper.
 * (similar to string descriptor but UTF-8 instead of UTF-16)
 */
#define USB_URL_DESC(scheme, str)                                  \
	(const void *)&(const struct {                             \
		uint8_t _len;                                      \
		uint8_t _type;                                     \
		uint8_t _scheme;                                   \
		char _data[sizeof(str)];                           \
	})                                                         \
	{                                                          \
		/* Total size of the descriptor is :               \
		 * size of the UTF-8 text plus the len/type fields \
		 * minus the string 0-termination                  \
		 */                                                \
		sizeof(str) + 3 - 1, USB_DT_WEBUSB_URL,            \
			USB_URL_SCHEME_##scheme, str               \
	}

/*
 * Extended Compat ID OS Feature Descriptor Specification for Windows v1.0 USB
 * Descriptors.
 */
#define USB_MS_STRING_DESC_VENDOR_CODE 0x2
#define USB_MS_EXT_COMPATIBLE_ID_INDEX 0x4
#define USB_GET_MS_DESCRIPTOR 0xEE
#define USB_MS_COMPAT_ID 'W', 'I', 'N', 'U', 'S', 'B'
#define USB_MS_COMPAT_ID_FUNCTION 1

struct usb_function_section {
	uint8_t bFirstInterfaceNumber;
	uint8_t reserved_1;
	uint8_t compatible_id[8];
	uint8_t subCompatibleID[8];
	uint8_t reserved_2[6];
} __packed;

struct usb_ms_ext_compat_id_desc {
	uint32_t dwLength;
	uint16_t bcdVersion;
	uint16_t wIndex;
	uint8_t bCount;
	uint8_t Reserved[7];
	/*
	 * The spec allows for multiple function sections to be included, but
	 * the only current use case requires just one function section to
	 * notify Windows to use the WINUSB driver.
	 */
	struct usb_function_section function[USB_MS_COMPAT_ID_FUNCTION];
} __packed;

/* Helpers for descriptors */

#define WIDESTR(quote) WIDESTR2(quote)
#define WIDESTR2(quote) u##quote

#define USB_STRING_DESC(str)                                              \
	(const void *)&(const struct {                                    \
		uint8_t _len;                                             \
		uint8_t _type;                                            \
		uint16_t _data[sizeof(str)];                              \
	})                                                                \
	{                                                                 \
		/* Total size of the descriptor is :                      \
		 * size of the UTF-16 text plus the len/type fields       \
		 * minus the string 0-termination                         \
		 */                                                       \
		sizeof(WIDESTR(str)) + 2 - 2, USB_DT_STRING, WIDESTR(str) \
	}

/*
 * Macro to generate a string descriptor used by Windows OS which instructs
 * windows to request a MS Compatible ID Descriptor and then enables Windows OS
 * to load the correct driver for a USB-EP
 */
#define USB_MS_STRING_DESC(str)                                                \
	((const void *)&(const struct {                                        \
		uint8_t _len;                                                  \
		uint8_t _type;                                                 \
		uint16_t _data[sizeof(str) - 1];                               \
		uint16_t _vendor;                                              \
	}){                                                                    \
		/* Total size of the descriptor is :                           \
		 * size of the UTF-16 text plus the len/type fields            \
		 * plus 2 bytes for vendor code minus the string 0-termination \
		 */                                                            \
		sizeof(WIDESTR(str)) + 2 - 2 + 2,                              \
		USB_DT_STRING,                                                 \
		WIDESTR(str),                                                  \
		USB_MS_STRING_DESC_VENDOR_CODE,                                \
	})

#ifdef CONFIG_USB_SERIALNO
/* String Descriptor for USB, for editable strings. */
struct usb_string_desc {
	uint8_t _len;
	uint8_t _type;
	uint16_t _data[CONFIG_SERIALNO_LEN];
};
#define USB_WR_STRING_DESC(str)                                         \
	(&(struct usb_string_desc){                                     \
		/* As above, two bytes metadata, no null terminator. */ \
		sizeof(WIDESTR(str)) + 2 - 2, USB_DT_STRING, WIDESTR(str) })
extern struct usb_string_desc *usb_serialno_desc;
#endif

/* Use these macros for declaring descriptors, to order them properly */
#define USB_CONF_DESC_VAR(name, varname) \
	varname __keep                   \
		__attribute__((section(".rodata.usb_desc_" STRINGIFY(name))))
#define USB_CONF_DESC(name) USB_CONF_DESC_VAR(name, CONCAT2(usb_desc_, name))
#define USB_IFACE_DESC(num) USB_CONF_DESC(CONCAT3(iface, num, _0iface))
#define USB_CUSTOM_DESC_VAR(i, name, varname) \
	USB_CONF_DESC_VAR(CONCAT4(iface, i, _1, name), varname)
#define USB_CUSTOM_DESC(i, name) USB_CONF_DESC(CONCAT4(iface, i, _1, name))
#define USB_EP_DESC(i, num) USB_CONF_DESC(CONCAT4(iface, i, _2ep, num))

/* USB Linker data */
extern const uint8_t __usb_desc[];
extern const uint8_t __usb_desc_end[];
#define USB_DESC_SIZE (__usb_desc_end - __usb_desc)

/* These descriptors defined in board code */
extern const void *const usb_strings[];
extern const uint8_t usb_string_desc[];
/* USB string descriptor with the firmware version */
extern const void *const usb_fw_version;
extern const struct bos_context bos_ctx;
extern const void *webusb_url;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_DESCRIPTOR_H */
