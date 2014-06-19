/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SCSI definitions.
 */

#ifndef USB_MS_SCSI_H
#define USB_MS_SCSI_H

#define SCSI_MAX_LUN					0

/* Status values */
#define SCSI_STATUS_GOOD				0x00
#define SCSI_STATUS_CHECK_CONDITION			0x02
#define SCSI_STATUS_CONDITION_MET			0x04
#define SCSI_STATUS_BUSY				0x08
#define SCSI_STATUS_RESERVATION_CONFLICT		0x18
#define SCSI_STATUS_TASK_SET_FULL			0x28
#define SCSI_STATUS_ACA_ACTIVE				0x30
#define SCSI_STATUS_TASK_ABORTED			0x40

/* Not part of standard, indicates operation not complete*/
#define SCSI_STATUS_CONTINUE				0xFF

/* Sense key values */
#define SCSI_SENSE_NO_SENSE				0x0
#define SCSI_SENSE_RECOVERED_ERROR			0x1
#define SCSI_SENSE_NOT_READY				0x2
#define SCSI_SENSE_MEDIUM_ERROR				0x3
#define SCSI_SENSE_HARDWARE_ERROR			0x4
#define SCSI_SENSE_ILLEGAL_REQUEST			0x5
#define SCSI_SENSE_UNIT_ATTENTION			0x6
#define SCSI_SENSE_DATA_PROTECT				0x7
#define SCSI_SENSE_BLANK_CHECK				0x8
#define SCSI_SENSE_VENDOR_SPECIFIC			0x9
#define SCSI_SENSE_COPY_ABORTED				0xa
#define SCSI_SENSE_ABORTED_COMMAND			0xb
#define SCSI_SENSE_VOLUME_OVERFLOW			0xd
#define SCSI_SENSE_MISCOMPARE				0xe
#define SCSI_SENSE_COMPLETED				0xf

/* Additional sense code (ASC) and additional sense code qualifier (ASCQ)
 * fields. Stored as ASC | ACSQ */
#define SCSI_SENSE_CODE_NONE				((0x00 << 4) | 0x00)
#define SCSI_SENSE_CODE_INVALID_COMMAND_OPERATION_CODE	((0x20 << 4) | 0x00)
#define SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB		((0x24 << 4) | 0x00)
#define SCSI_SENSE_CODE_UNRECOVERED_READ_ERROR		((0x11 << 4) | 0x00)
#define SCSI_SENSE_CODE_NOT_READY			((0x04 << 4) | 0x00)
#define SCSI_SENSE_CODE_COMMAND_TO_LUN_FAILED		((0x6e << 4) | 0x00)
#define SCSI_SENSE_CODE_LBA_OUT_OF_RANGE		((0x21 << 4) | 0x00)
#define SCSI_SENSE_CODE_WRITE_PROTECTED			((0x27 << 4) | 0x00)
#define SCSI_SENSE_CODE_TIMEOUT				((0x3e) | 0x02)
#define SCSI_SENSE_CODE_ASC(x)				((x & 0xf0) >> 8)
#define SCSI_SENSE_CODE_ASCQ(x)				(x & 0x0f)

/* Version descriptor values */
#define SCSI_VERSION_SBC3				0x04, 0xc0
#define SCSI_VERSION_SPC4				0x04, 0x60

/* Vital product data page codes */
#define SCSI_VPD_CODE_SUPPORTED_PAGES			0x00
#define SCSI_VPD_CODE_SERIAL_NUMBER			0x80
#define SCSI_VPD_CODE_DEVICE_ID				0x83

/* Mode pages */
#define SCSI_MODE_PAGE_ALL				0x3f
/* Response values for fixed-format sense data */
#define SCSI_SENSE_RESPONSE_CURRENT			0x70
#define SCSI_SENSE_RESPONSE_DEFERRED			0x71

/* Size of various SCSI data structures */
#define SCSI_CDB6_SIZE					6
#define SCSI_CDB10_SIZE					10
#define SCSI_CDB12_SIZE					12

/* Block size for LBA addressing */
#define SCSI_BLOCK_SIZE_BYTES				(4 * 1024)

/* USB mass storage SCSI state machine */
enum usb_ms_scsi_state {
	USB_MS_SCSI_STATE_IDLE,
	USB_MS_SCSI_STATE_PARSE,
	USB_MS_SCSI_STATE_DATA_IN,
	USB_MS_SCSI_STATE_DATA_OUT,
	USB_MS_SCSI_STATE_REPLY,
};

/* Structure defining sense key entry */
struct scsi_sense_entry {
	uint8_t key; /* Sense Key */
	uint8_t ASC; /* Additional Sense Code */
	uint8_t ASCQ; /* Additional Sense Qualifier */
};

/* Structure defining read format capacities response */
struct scsi_capacity_list_response {
	uint32_t header; /* Reserved | List Length */
	uint32_t blocks; /* Number of Blocks */
	uint32_t block_length; /* Reserved | Descriptor Code | Block Length */
};

/* USB endpoint buffers */
extern usb_uint ms_ep_tx[USB_MS_PACKET_SIZE] __usb_ram;
extern usb_uint ms_ep_rx[USB_MS_PACKET_SIZE] __usb_ram;

int scsi_parse(uint8_t *block, uint8_t length);
void scsi_reset(void);

#endif /* USB_MS_SCSI_H */
