/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __USB_DFU_H
#define __USB_DFU_H

#include "common.h"

/* Universal Serial Bus Device Class Specification for Device Firmware Upgrade
 * Version 1.1
 * https://www.usb.org/sites/default/files/DFU_1.1.pdf
 */

#define USB_DFU_RUNTIME_SUBCLASS    0x01
#define USB_DFU_RUNTIME_PROTOCOL    0x01

#define USB_DFU_RUNTIME_DESC_ATTR_CAN_DOWNLOAD       BIT(0)
#define USB_DFU_RUNTIME_DESC_ATTR_CAN_UPLOAD         BIT(1)
#define USB_DFU_RUNTIME_DESC_ATTR_MANIFEST_TOLERANT  BIT(2)
#define USB_DFU_RUNTIME_DESC_ATTR_WILL_DETACH        BIT(3)

#define USB_DFU_RUNTIME_DESC_ATTRS \
	(USB_DFU_RUNTIME_DESC_ATTR_CAN_DOWNLOAD | \
	USB_DFU_RUNTIME_DESC_ATTR_CAN_UPLOAD | \
	USB_DFU_RUNTIME_DESC_ATTR_WILL_DETACH)

#define USB_DFU_RUNTIME_DESC_SIZE            9
#define USB_DFU_RUNTIME_DESC_FUNCTIONAL      0x21
#define USB_DFU_RUNTIME_DESC_DETACH_TIMEOUT  0xffff
#define USB_DFU_RUNTIME_DESC_TRANSFER_SIZE   64
#define USB_DFU_RUNTIME_DESC_DFU_VERSION     0x0022

/* DFU states */
#define USB_DFU_RUNTIME_STATE_APP_IDLE      0
#define USB_DFU_RUNTIME_STATE_APP_DETACH    1

/* DFU status */
#define USB_DFU_RUNTIME_STATUS_OK           0

/* DFU Request types */
#define USB_DFU_RUNTIME_REQ_DETACH          0
#define USB_DFU_RUNTIME_REQ_DNLOAD          1
#define USB_DFU_RUNTIME_REQ_UPLOAD          2
#define USB_DFU_RUNTIME_REQ_GET_STATUS      3
#define USB_DFU_RUNTIME_REQ_CLR_STATUS      4
#define USB_DFU_RUNTIME_REQ_GET_STATE       5
#define USB_DFU_RUNTIME_REQ_ABORT           6


/* DFU Functional Descriptor  */
struct usb_runtime_dfu_functional_desc {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bmAttributes;
	uint16_t wDetachTimeOut;
	uint16_t wTransferSize;
	uint16_t bcdDFUVersion;
} __packed;

/* DFU response packets */
struct usb_runtime_dfu_get_status_resp {
	uint8_t bStatus;
	uint8_t bwPollTimeout[3];
	uint8_t bState;
	uint8_t iString;
} __packed;

struct usb_runtime_dfu_get_state_resp {
	uint8_t bState;
} __packed;

#endif /* __USB_DFU_H */
