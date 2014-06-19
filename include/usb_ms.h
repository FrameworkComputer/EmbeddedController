/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB mass storage definitions.
 */

#ifndef USB_MS_H
#define USB_MS_H

#define USB_MS_SUBCLASS_RBC				0x01
#define USB_MS_SUBCLASS_MMC5				0x02
#define USB_MS_SUBCLASS_UFI				0x04
#define USB_MS_SUBCLASS_SCSI				0x06
#define USB_MS_SUBCLASS_LSDFS				0x07
#define USB_MS_SUBCLASS_IEEE1667			0x08

#define USB_MS_PROTOCOL_CBI_INTERRUPT			0x00
#define USB_MS_PROTOCOL_CBI				0x01
#define USB_MS_PROTOCOL_BBB				0x50
#define USB_MS_PROTOCOL_UAS				0x62

#define USB_MS_PACKET_SIZE				(USB_MAX_PACKET_SIZE)

/* USB Mass Storage Command Block Wrapper */
struct usb_ms_cbw {
	uint32_t signature;
	uint32_t tag;
	uint32_t data_transfer_length;
	uint8_t flags;
	uint8_t LUN;
	uint8_t length;
	uint8_t command_block[16];
} __packed;
#define USB_MS_CBW_LENGTH				31

#define USB_MS_CBW_SIGNATURE				0x43425355
#define USB_MS_CBW_DATA_IN				(1 << 7)

/* USB Mass Storage Command Status Wrapper */
struct usb_ms_csw {
	uint32_t signature;
	uint32_t tag;
	uint32_t data_residue;
	uint8_t status;
} __packed;
#define USB_MS_CSW_LENGTH				13

#define UBS_MS_CSW_SIGNATURE				0x53425355
#define USB_MS_CSW_CMD_PASSED				0x0
#define USB_MS_CSW_CMD_FAILED				0x1
#define USB_MS_CSW_CMD_PHASE_ERR			0x2

#define USB_MS_REQ_RESET				0xff
#define USB_MS_REQ_GET_MAX_LUN				0xfe

#define USB_MS_EVENT_TX					(1 << 0)
#define USB_MS_EVENT_RX					(1 << 1)

/* Maximum number of supported LUN's, defined in SCSI file */
extern const uint8_t max_lun;

#endif /* USB_MS_H */
