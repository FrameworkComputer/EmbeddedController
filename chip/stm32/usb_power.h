/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_POWER_H
#define __CROS_EC_USB_POWER_H

/* Power monitoring USB interface for Chrome EC */

#include "compile_time_macros.h"
#include "hooks.h"
#include "usb_descriptor.h"
#include "usb_hw.h"

/*
 * Command:
 *
 *     Commands are a 16 bit value, with optional command dependent data.
 *     +--------------+-----------------------------------+
 *     | command : 2B |					  |
 *     +--------------+-----------------------------------+
 *
 *     Responses are an 8 bit status value, with optional data.
 *     +----------+-----------------------------------+
 *     | res : 1B |				      |
 *     +----------+-----------------------------------+
 *
 *     reset:	0x0000
 *     +--------+
 *     | 0x0000 |
 *     +--------+
 *
 *     stop:	0x0001
 *     +--------+
 *     | 0x0001 |
 *     +--------+
 *
 *     addina:	0x0002
 *     +--------+--------------------------+-------------+--------------+-----------+--------+
 *     | 0x0002 | 1B: 4b: extender 4b: bus | 1B:INA type | 1B: INA addr | 1B: extra | 4B: Rs |
 *     +--------+--------------------------+-------------+--------------+-----------+--------+
 *
 *     start:	0x0003
 *     +--------+----------------------+
 *     | 0x0003 | 4B: integration time |
 *     +--------+----------------------+
 *
 *     start response:
 *     +-------------+-----------------------------+
 *     | status : 1B | Actual integration time: 4B |
 *     +-------------+-----------------------------+
 *
 *     next:	0x0004
 *     +--------+
 *     | 0x0004 |
 *     +--------+
 *
 *     next response:
 *     +-------------+----------+----------------+----------------------------+
 *     | status : 1B | size: 1B | timestamp : 8B | payload : may span packets |
 *     +-------------+----------+----------------+----------------------------+
 *
 *     settime:	0x0005
 *     +--------+---------------------+
 *     | 0x0005 | 8B: Wall clock time |
 *     +--------+---------------------+
 *
 *
 *     Status: 1 byte status
 *
 *	 0x00: Success
 *	 0x01: I2C Error
 *	 0x02: Overflow
 *	     This can happen if data acquisition is faster than USB reads.
 *	 0x03: No configuration set.
 *	 0x04: No active capture.
 *	 0x05: Timeout.
 *	 0x06: Busy, outgoing queue is empty.
 *	 0x07: Size, command length is incorrect for command type..
 *	 0x08: More INAs specified than board limit.
 *	 0x09: Invalid input, eg. invalid INA type.
 *	 0x80: Unknown error
 *
 *     size: 1 byte incoming INA reads count
 *
 *     timestamp: 4 byte timestamp associated with these samples
 *
 */

/* 8b status field. */
enum usb_power_error {
	USB_POWER_SUCCESS		= 0x00,
	USB_POWER_ERROR_I2C		= 0x01,
	USB_POWER_ERROR_OVERFLOW	= 0x02,
	USB_POWER_ERROR_NOT_SETUP	= 0x03,
	USB_POWER_ERROR_NOT_CAPTURING	= 0x04,
	USB_POWER_ERROR_TIMEOUT		= 0x05,
	USB_POWER_ERROR_BUSY		= 0x06,
	USB_POWER_ERROR_READ_SIZE	= 0x07,
	USB_POWER_ERROR_FULL		= 0x08,
	USB_POWER_ERROR_INVAL		= 0x09,
	USB_POWER_ERROR_UNKNOWN		= 0x80,
};

/* 16b command field. */
enum usb_power_command {
	USB_POWER_CMD_RESET	= 0x0000,
	USB_POWER_CMD_STOP	= 0x0001,
	USB_POWER_CMD_ADDINA	= 0x0002,
	USB_POWER_CMD_START	= 0x0003,
	USB_POWER_CMD_NEXT	= 0x0004,
	USB_POWER_CMD_SETTIME	= 0x0005,
};

/* Addina "INA Type" field. */
enum usb_power_ina_type {
	USBP_INA231_POWER	= 0x01,
	USBP_INA231_BUSV	= 0x02,
	USBP_INA231_CURRENT	= 0x03,
	USBP_INA231_SHUNTV	= 0x04,
};

/* Internal state machine values */
enum usb_power_states {
	USB_POWER_STATE_OFF	= 0,
	USB_POWER_STATE_SETUP,
	USB_POWER_STATE_CAPTURING,
};

#define USB_POWER_MAX_READ_COUNT 64
#define USB_POWER_MIN_CACHED 10

struct usb_power_ina_cfg {
	/*
	 * Relevant config for INA usage.
	 */
	/* i2c bus. TODO(nsanders): specify what kind of index. */
	int port;
	/* 7-bit i2c addr */
	uint16_t addr_flags;

	/* Base voltage. mV */
	int mv;

	/* Shunt resistor. mOhm */
	int rs;
	/* uA per div as reported from INA */
	int scale;

	/* Is this power, shunt voltage, bus voltage, or current? */
	int type;
	/* Is this INA returning the one value only and can use readagain? */
	int shared;
};


struct __attribute__ ((__packed__)) usb_power_report {
	uint8_t status;
	uint8_t size;
	uint64_t timestamp;
	uint16_t power[USB_POWER_MAX_READ_COUNT];
};

/* Must be 4 byte aligned */
#define USB_POWER_RECORD_SIZE(ina_count)				\
	((((sizeof(struct usb_power_report)				\
	- (sizeof(uint16_t) * USB_POWER_MAX_READ_COUNT)			\
	+ (sizeof(uint16_t) * (ina_count))) + 3) / 4) * 4)

#define USB_POWER_DATA_SIZE						\
	(sizeof(struct usb_power_report) * (USB_POWER_MIN_CACHED + 1))
#define USB_POWER_MAX_CACHED(ina_count)					\
	(USB_POWER_DATA_SIZE / USB_POWER_RECORD_SIZE(ina_count))


struct usb_power_state {
	/*
	 * The power data acquisition must be setup, then started, in order to
	 * return data.
	 * States are OFF, SETUP, and CAPTURING.
	 */
	int state;

	struct usb_power_ina_cfg ina_cfg[USB_POWER_MAX_READ_COUNT];
	int ina_count;
	int integration_us;
	/* Start of sampling. */
	uint64_t base_time;
	/* Offset between microcontroller timestamp and host wall clock. */
	uint64_t wall_offset;

	/* Cached power reports for sending on USB. */
	/* Actual backing data for variable sized record queue. */
	uint8_t reports_data_area[USB_POWER_DATA_SIZE];
	/* Size of power report struct for this config. */
	int stride_bytes;
	/* Max power records storeable in this config */
	int max_cached;
	struct usb_power_report *reports;

	/* Head and tail pointers for output ringbuffer */
	/* Head adds newly probed power data. */
	int reports_head;
	/* Tail contains oldest records not yet sent to USB */
	int reports_tail;
	/* Xmit_active -> tail is active usb DMA */
	int reports_xmit_active;

	/* Pointers to RAM. */
	uint8_t rx_buf[USB_MAX_PACKET_SIZE];
	uint8_t tx_buf[USB_MAX_PACKET_SIZE * 4];
};


/*
 * Compile time Per-USB gpio configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB gpio.  This structure binds
 * together all information required to operate a USB gpio.
 */
struct usb_power_config {
	/* In RAM state of the USB power interface. */
	struct usb_power_state *state;

	/* USB endpoint state.*/
	struct dwc_usb_ep *ep;

	/* Interface and endpoint indicies. */
	int interface;
	int endpoint;

	/* Deferred function to call to handle power request. */
	const struct deferred_data *deferred;
	const struct deferred_data *deferred_cap;
};

struct __attribute__ ((__packed__)) usb_power_command_start {
	uint16_t command;
	uint32_t integration_us;
};

struct __attribute__ ((__packed__)) usb_power_command_addina {
	uint16_t command;
	uint8_t port;
	uint8_t type;
	uint8_t addr_flags;
	uint8_t extra;
	uint32_t rs;
};

struct __attribute__ ((__packed__)) usb_power_command_settime {
	uint16_t command;
	uint64_t time;
};

union usb_power_command_data {
	uint16_t command;
	struct usb_power_command_start start;
	struct usb_power_command_addina addina;
	struct usb_power_command_settime settime;
};


/*
 * Convenience macro for defining a USB INA Power driver.
 *
 * NAME is used to construct the names of the trampoline functions and the
 * usb_power_config struct, the latter is just called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * driver.
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 */
#define USB_POWER_CONFIG(NAME,						\
		       INTERFACE,					\
		       ENDPOINT)					\
	static void CONCAT2(NAME, _deferred_tx_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_tx_));			\
	static void CONCAT2(NAME, _deferred_rx_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_rx_));			\
	static void CONCAT2(NAME, _deferred_cap_)(void);		\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_cap_));		\
	struct usb_power_state CONCAT2(NAME, _state_) = {		\
		.state = USB_POWER_STATE_OFF,				\
		.ina_count = 0,						\
		.integration_us = 0,					\
		.reports_head = 0,					\
		.reports_tail = 0,					\
		.wall_offset = 0,					\
	};								\
	static struct dwc_usb_ep CONCAT2(NAME, _ep_ctl) = {		\
		.max_packet = USB_MAX_PACKET_SIZE,			\
		.tx_fifo = ENDPOINT,					\
		.out_pending = 0,					\
		.out_data = 0,						\
		.out_databuffer = 0,					\
		.out_databuffer_max = 0,				\
		.rx_deferred = &CONCAT2(NAME, _deferred_rx__data),	\
		.in_packets = 0,					\
		.in_pending = 0,					\
		.in_data = 0,						\
		.in_databuffer = 0,					\
		.in_databuffer_max = 0,					\
		.tx_deferred = &CONCAT2(NAME, _deferred_tx__data),	\
	};								\
	struct usb_power_config const NAME = {				\
		.state     = &CONCAT2(NAME, _state_),			\
		.ep	= &CONCAT2(NAME, _ep_ctl),			\
		.interface = INTERFACE,					\
		.endpoint  = ENDPOINT,					\
		.deferred_cap  = &CONCAT2(NAME, _deferred_cap__data),	\
	};								\
	const struct usb_interface_descriptor				\
	USB_IFACE_DESC(INTERFACE) = {					\
		.bLength	    = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 0,				\
		.bNumEndpoints      = 2,				\
		.bInterfaceClass    = USB_CLASS_VENDOR_SPEC,		\
		.bInterfaceSubClass = USB_SUBCLASS_GOOGLE_POWER,	\
		.bInterfaceProtocol = USB_PROTOCOL_GOOGLE_POWER,	\
		.iInterface	 = 0,				\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 0) = {					\
		.bLength	  = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = 0x80 | ENDPOINT,			\
		.bmAttributes     = 0x02 /* Bulk IN */,			\
		.wMaxPacketSize   = USB_MAX_PACKET_SIZE,		\
		.bInterval	= 1,					\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 1) = {					\
		.bLength	  = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = ENDPOINT,				\
		.bmAttributes     = 0x02 /* Bulk OUT */,		\
		.wMaxPacketSize   = USB_MAX_PACKET_SIZE,		\
		.bInterval	= 0,					\
	};								\
	static void CONCAT2(NAME, _ep_tx_)   (void) { usb_epN_tx(ENDPOINT); } \
	static void CONCAT2(NAME, _ep_rx_)   (void) { usb_epN_rx(ENDPOINT); } \
	static void CONCAT2(NAME, _ep_event_)(enum usb_ep_event evt)	\
	{								\
			usb_power_event(&NAME, evt);			\
	}								\
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx_),				\
		       CONCAT2(NAME, _ep_rx_),				\
		       CONCAT2(NAME, _ep_event_));			\
	static void CONCAT2(NAME, _deferred_tx_)(void)			\
	{ usb_power_deferred_tx(&NAME); }				\
	static void CONCAT2(NAME, _deferred_rx_)(void)			\
	{ usb_power_deferred_rx(&NAME); }				\
	static void CONCAT2(NAME, _deferred_cap_)(void)			\
	{ usb_power_deferred_cap(&NAME); }


/*
 * Handle power request in a deferred callback.
 */
void usb_power_deferred_rx(struct usb_power_config const *config);
void usb_power_deferred_tx(struct usb_power_config const *config);
void usb_power_deferred_cap(struct usb_power_config const *config);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB GPIO driver.
 */
void usb_power_tx(struct usb_power_config const *config);
void usb_power_rx(struct usb_power_config const *config);
void usb_power_event(struct usb_power_config const *config,
		enum usb_ep_event evt);




#endif /* __CROS_EC_USB_DWC_POWER_H */

