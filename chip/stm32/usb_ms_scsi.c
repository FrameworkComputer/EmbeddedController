/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "spi.h"
#include "spi_flash.h"
#include "util.h"
#include "usb.h"
#include "usb_ms.h"
#include "usb_ms_scsi.h"

/*
 * Implements the SCSI-3 Block Commands (SBC-3) standard for
 * Direct Access Block Devices with respect to the
 * SCSI Primary Commands - 4 (SPC-4) standard.
 *
 * Note: Not all SPC-4 mandatory commands implemented; only LUN 0 supported.
 */

/* Command operation codes */
#define SCSI_INQUIRY						0x12
#define SCSI_MODE_SENSE6					0x1a
#define SCSI_READ10						0x28
#define SCSI_READ_CAPACITY10					0x25
#define SCSI_READ_FORMAT_CAPACITIES				0x23
#define SCSI_REPORT_LUNS					0xa0
#define SCSI_REQUEST_SENSE					0x03
#define SCSI_START_STOP_UNIT					0x1b
#define SCSI_SYNCHRONIZE_CACHE10				0x35
#define SCSI_TEST_UNIT_READY					0x00
#define SCSI_WRITE10						0x2a

#define SCSI_STANDARD_INQUIRY_SIZE				62
/* Standard inquiry response */
static const uint8_t scsi_standard_inquiry[] = {
	0x00, /* Peripheral Qualifier | Peripheral Device Type (SBC-3) */
	(1 << 7), /* RMB | LU_CONG | Reserved */
	0x06, /* Version (SPC-4) */
	0x02, /* Reserved | Reserved | NormACA | HiSup | Response Data Format */
	(SCSI_STANDARD_INQUIRY_SIZE - 5), /* Additional Length */
	0x00, /* SCCS | ACC | TPGS | 3PC | Reserved | Protect */
	0x00, /* Obsolete | EncServ | VS | MultiP |
			Obsolete | Reserved | Reserved | Addr16 */
	0x00, /* Obsolete | Reserved | WBUS16 | Syncs |
			Obsolete | Reserved | CmdQue | VS */
	'G', 'O', 'O', 'G', 'L', 'E', '\0', '\0', /* Vendor ID */
	'S', 'P', 'I', 'F', 'l', 'a', 's', 'h', /* Product ID */
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', /* Lot Number */
	'1', '.', '0' , '0', /* Product Revision Level */
	'\0', '\0', '\0', '\0', /* Vendor Specific */
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', /* Vendor Specific */
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', /* Vendor Specific */
	0x00, /* Reserved | Clocking | QAS | IUS */
	0x00, /* Reserved */
	SCSI_VERSION_SBC3, /* Version 1 */
	SCSI_VERSION_SPC4, /* Version 2 */
};
BUILD_ASSERT(sizeof(scsi_standard_inquiry) == SCSI_STANDARD_INQUIRY_SIZE);

#define SCSI_VPD_SUPPORTED_PAGES_SIZE				7
/* Vital product data (VPD) response for supported VPD pages */
static const uint8_t scsi_vpd_supported_pages[] = {
	0x00, /* Peripheral Qualifier | Peripheral Device Type (SBC-3) */
	SCSI_VPD_CODE_SUPPORTED_PAGES, /* Page Code */
	0x00, /* Page Length */
	(SCSI_VPD_SUPPORTED_PAGES_SIZE - 4), /* Page Length */
	SCSI_VPD_CODE_SUPPORTED_PAGES, /* Supported VPD Pages */
	SCSI_VPD_CODE_SERIAL_NUMBER, /* Serial Number Page */
	SCSI_VPD_CODE_DEVICE_ID, /* Device ID Page */
};
BUILD_ASSERT(sizeof(scsi_vpd_supported_pages) == SCSI_VPD_SUPPORTED_PAGES_SIZE);

#define SCSI_VPD_SERIAL_NUMBER_SIZE				17
/* Vital product data (VPD) response for serial number page */
static const uint8_t scsi_vpd_serial_number[] = {
	0x00, /* Peripheral Qualifier | Peripheral Device Type (SBC-3) */
	SCSI_VPD_CODE_SERIAL_NUMBER, /* Page Code */
	0x00, /* Page Length */
	(SCSI_VPD_SERIAL_NUMBER_SIZE - 4), /* Page Length */
	'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', /* Serial Number */
	'\0', '\0', '\0', '\0', /* Serial Number */
	'\0', /* Serial Number */
};
BUILD_ASSERT(sizeof(scsi_vpd_serial_number) == SCSI_VPD_SERIAL_NUMBER_SIZE);

#define SCSI_VPD_DESIGNATOR_LENGTH				21
#define SCSI_VPD_DEVICE_ID_SIZE		(SCSI_VPD_DESIGNATOR_LENGTH + 4)
/* Vital product data (VPD) response for device ID page */
static const uint8_t scsi_vpd_device_id[] = {
	0x00, /* Peripheral Qualifier | Peripheral Device Type (SBC-3) */
	SCSI_VPD_CODE_DEVICE_ID, /* Page Code */
	0x00, /* Designation Descriptor Length */
	(SCSI_VPD_DEVICE_ID_SIZE - 3), /* Designation Descriptor Length */
	0x02, /* Protocol Identifier | Code Set (ASCII) */
	0x01, /* PIV | Reserved | Association | Designator Type (T10) */
	0x00, /* Reserved */
	(SCSI_VPD_DESIGNATOR_LENGTH - 3), /* Designator Length */
	'G', 'O', 'O', 'G', 'L', 'E', '\0', '\0', /* Vendor ID */
	'S', 'P', 'I', 'F', 'l', 'a', 's', 'h', /* Vendor Specific ID */
	'\0', /* Vendor Specific ID */
};
BUILD_ASSERT(sizeof(scsi_vpd_device_id) == SCSI_VPD_DEVICE_ID_SIZE);

/* Capacity list response for read format capacities */
static const struct scsi_capacity_list_response scsi_capacity_list = {
	.header = 0x08, /* Reserved | List Length */
	/* Number of Blocks */
	.blocks = (CONFIG_SPI_FLASH_SIZE / SCSI_BLOCK_SIZE_BYTES),
	/* Reserved | Descriptor Code | Block Length */
	.block_length = (0x02 << 24) | SCSI_BLOCK_SIZE_BYTES,
};

/* Current state of SCSI state machine */
static enum usb_ms_scsi_state state = USB_MS_SCSI_STATE_IDLE;
static int buffer;
static int offset;
static int bytes;
static uint8_t op;

/* Current sense key */
static struct scsi_sense_entry scsi_sense_data;

/* Local buffer for caching */
static uint8_t temp_buf[CONFIG_USB_MS_BUFFER_SIZE];

static void scsi_sense_code(uint8_t sense, uint16_t code)
{
	scsi_sense_data.key = sense;
	scsi_sense_data.ASC = SCSI_SENSE_CODE_ASC(code);
	scsi_sense_data.ASCQ = SCSI_SENSE_CODE_ASCQ(code);
}

static int scsi_verify_cdb6(uint8_t *block, uint8_t in_len)
{
	/* message too short */
	if (in_len < SCSI_CDB6_SIZE) {
		scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
			SCSI_SENSE_CODE_NONE);
		return -1;
	}

	/* NACA bit not supported */
	if (block[5] & 0x4) {
		scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
			SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);
		return -1;
	}

	return 0;
}

static int scsi_verify_cdb10(uint8_t *block, uint8_t in_len)
{
	/* message too short */
	if (in_len < SCSI_CDB10_SIZE) {
		scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
			SCSI_SENSE_CODE_NONE);
		return -1;
	}

	/* NACA bit not supported */
	if (block[9] & 0x4) {
		scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
			SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);
		return -1;
	}

	return 0;
}

/*
 * Required by SPC-4.
 */
static void scsi_inquiry(uint8_t *block, uint8_t in_len)
{
	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb6(block, in_len))
			return;

		/* EVPD bit set */
		if (block[1] & 0x1) {
			/* lookup VPD page */
			switch (block[2]) {
			case SCSI_VPD_CODE_SUPPORTED_PAGES:
				/* return supported pages */
				memcpy_usbram(ms_ep_tx,
					scsi_vpd_supported_pages,
					sizeof(scsi_vpd_supported_pages));
				/* truncate response */
				btable_ep[USB_EP_MS_TX].tx_count =
					MIN(block[3] << 8 | block[4],
					sizeof(scsi_vpd_supported_pages));
			break;
			case SCSI_VPD_CODE_SERIAL_NUMBER:
				/* return serial number response */
				memcpy_usbram(ms_ep_tx,
					scsi_vpd_serial_number,
					sizeof(scsi_vpd_serial_number));

				/* copy STM32 LOT_NUM for serial number */
				memcpy(temp_buf,
					((uint8_t *) STM32_UNIQUE_ID) + 4 + 1,
					7 * sizeof(uint8_t));
				/* copy STM32 WAF_NUM for serial number */
				temp_buf[7] = ((uint8_t *) STM32_UNIQUE_ID)[4];
				/* copy STM32 UID for serial number */
				memcpy(temp_buf + 8,
					((uint8_t *) STM32_UNIQUE_ID),
					4 * sizeof(uint8_t));

				/* copy actual serial number */
				memcpy_usbram((usb_uint *)
					(((uint8_t *) ms_ep_tx) + 4),
					temp_buf,
					12 * sizeof(uint8_t));

				/* truncate response */
				btable_ep[USB_EP_MS_TX].tx_count =
					MIN(block[3] << 8 | block[4],
					sizeof(scsi_vpd_serial_number));
			break;
			case SCSI_VPD_CODE_DEVICE_ID:
				/* return device id */
				memcpy_usbram(ms_ep_tx,
					scsi_vpd_device_id,
					sizeof(scsi_vpd_device_id));
				/* truncate response */
				btable_ep[USB_EP_MS_TX].tx_count =
					MIN(block[3] << 8 | block[4],
					sizeof(scsi_vpd_device_id));
			break;
			default:
				/* not supported */
				return scsi_sense_code(
					SCSI_SENSE_ILLEGAL_REQUEST,
					SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);
			break;
			}
		/* EVPD not set but page code set */
		} else if (block[2]) {
			return scsi_sense_code(
				SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);
		} else {
			/* return standard inquiry data */
			memcpy_usbram(ms_ep_tx, scsi_standard_inquiry,
				sizeof(scsi_standard_inquiry));

			/* copy STM32 LOT_NUM for vendor specific id */
			memcpy_usbram((usb_uint *)
				(((uint8_t *) ms_ep_tx) + 24),
				((uint8_t *) STM32_UNIQUE_ID) + 4 + 1,
				7 * sizeof(uint8_t));

			/* truncate response */
			btable_ep[USB_EP_MS_TX].tx_count =
				MIN(block[3] << 8 | block[4],
				sizeof(scsi_standard_inquiry));
		}

	} else if (state == USB_MS_SCSI_STATE_DATA_OUT)
		state = USB_MS_SCSI_STATE_REPLY;

	return scsi_sense_code(SCSI_SENSE_NO_SENSE,
		SCSI_SENSE_CODE_NONE);
}

static void scsi_mode_sense6(uint8_t *block, uint8_t in_len)
{
	uint8_t response[4];

	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb6(block, in_len))
			return;

		/* response exceeds allocation length */
		if (block[4] < sizeof(response))
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		memset(response, 0, sizeof(response));
		/* set WP bit if necessary */
		response[2] = spi_flash_check_protect(0,
			CONFIG_SPI_FLASH_SIZE) ?
			(1 << 7) : 0;

		memcpy_usbram(ms_ep_tx, (uint8_t *) response,
			sizeof(response));
		btable_ep[USB_EP_MS_TX].tx_count = sizeof(response);
	} else if (state == USB_MS_SCSI_STATE_DATA_OUT)
		state = USB_MS_SCSI_STATE_REPLY;

	return scsi_sense_code(SCSI_SENSE_NO_SENSE,
		SCSI_SENSE_CODE_NONE);
}

/*
 * Required by SPC-4.
 */
static void scsi_read10(uint8_t *block, uint8_t in_len)
{
	int rv;
	int read_len;

	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb10(block, in_len))
			return;

		/* RELADR bit not supported */
		if (block[1] & 0x1)
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		offset = SCSI_BLOCK_SIZE_BYTES *
			(block[2] << 24 | block[3] << 16
				| block[4] << 8 | block[5]);
		bytes = SCSI_BLOCK_SIZE_BYTES *
			(block[7] << 8 | block[8]);

		/* Wait for any previous operation to complete */
		rv = spi_flash_wait();
		if (rv == EC_ERROR_TIMEOUT)
			return scsi_sense_code(
				SCSI_SENSE_HARDWARE_ERROR,
				SCSI_SENSE_CODE_TIMEOUT);
	}

	if (state == USB_MS_SCSI_STATE_DATA_OUT) {
		/* nothing left to read */
		if (!bytes) {
			state = USB_MS_SCSI_STATE_REPLY;
			return scsi_sense_code(SCSI_SENSE_NO_SENSE,
				SCSI_SENSE_CODE_NONE);
		}

		/* read in multiples of USB_MS_PACKET_SIZE, then bytes */
		read_len = MIN(bytes, USB_MS_PACKET_SIZE);

		rv = spi_flash_read(temp_buf, offset, read_len);
		/* invalid address */
		if (rv == EC_ERROR_INVAL)
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_LBA_OUT_OF_RANGE);
		else if (rv != EC_SUCCESS)
			return scsi_sense_code(SCSI_SENSE_HARDWARE_ERROR,
				SCSI_SENSE_CODE_UNRECOVERED_READ_ERROR);

		/* temp buffer for chip addressing issues */
		memcpy_usbram(ms_ep_tx, temp_buf, read_len);
		offset += read_len;
		bytes -= read_len;

		btable_ep[USB_EP_MS_TX].tx_count = read_len;
	}

	return scsi_sense_code(SCSI_SENSE_NO_SENSE,
		SCSI_SENSE_CODE_NONE);
}

/*
 * Required by SPC-4.
 */
static void scsi_read_capacity10(uint8_t *block, uint8_t in_len)
{
	uint32_t response[2];

	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb10(block, in_len))
			return;

		/* RELADR bit not supported */
		if (block[1] & 0x1)
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		/* PMI bit or LBA not supported */
		if (block[2] | block[3] | block[4] |
			block[5] | (block[8] & 0x1))
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		/* compute LBA and block size, send in big endian */
		response[0] = __builtin_bswap32((CONFIG_SPI_FLASH_SIZE /
			SCSI_BLOCK_SIZE_BYTES) - 1);
		response[1] = __builtin_bswap32(SCSI_BLOCK_SIZE_BYTES);

		memcpy_usbram(ms_ep_tx, (uint8_t *) response,
			sizeof(response));
		btable_ep[USB_EP_MS_TX].tx_count = sizeof(response);
	} else if (state == USB_MS_SCSI_STATE_DATA_OUT)
		state = USB_MS_SCSI_STATE_REPLY;

	return scsi_sense_code(SCSI_SENSE_NO_SENSE,
		SCSI_SENSE_CODE_NONE);
}

/*
 * Used by UFI. Required by Windows XP.
 */
static void scsi_read_format_capacities(uint8_t *block, uint8_t in_len)
{
	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb10(block, in_len))
			return;

		memcpy_usbram(ms_ep_tx, (uint8_t *) &scsi_capacity_list,
			sizeof(scsi_capacity_list));
		btable_ep[USB_EP_MS_TX].tx_count = sizeof(scsi_capacity_list);
	} else if (state == USB_MS_SCSI_STATE_DATA_OUT)
		state = USB_MS_SCSI_STATE_REPLY;

	return scsi_sense_code(SCSI_SENSE_NO_SENSE,
		SCSI_SENSE_CODE_NONE);
}

/*
 * Required by SPC-4.
 */
static void scsi_report_luns(uint8_t *block, uint8_t in_len)
{
	uint32_t response[16];

	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb6(block, in_len))
			return;

		/* response exceeds allocation length */
		if ((block[3] << 8 | block[4]) < sizeof(response))
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		memset(response, 0, sizeof(response));
		/* one LUN in the list */
		response[3] = 1;

		/* return response */
		memcpy_usbram(ms_ep_tx, (uint8_t *) response,
			sizeof(response));
		btable_ep[USB_EP_MS_TX].tx_count = sizeof(response);
	} else if (state == USB_MS_SCSI_STATE_DATA_OUT)
		state = USB_MS_SCSI_STATE_REPLY;

	return scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

/*
 * Required by SPC-4.
 */
static void scsi_request_sense(uint8_t *block, uint8_t in_len)
{
	uint8_t response[18];

	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_OUT;

		/* terminate if fail to verify */
		if (scsi_verify_cdb6(block, in_len))
			return;

		/* response exceeds allocation length */
		if (block[4] < sizeof(response))
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		memset(response, 0, sizeof(response));
		/* Valid | Response Code */
		response[0] = SCSI_SENSE_RESPONSE_CURRENT;
		/* Filemark | EOM | ILI | SDAT_OVFL | Sense Key */
		response[2] = scsi_sense_data.key;
		/* Additional Sense Length */
		response[7] = ARRAY_SIZE(response) - 7;
		/* Additional Sense Code */
		response[12] = scsi_sense_data.ASC;
		/* Additional Sense Code Qualifier */
		response[13] = scsi_sense_data.ASCQ;

		/* return fixed format sense data */
		memcpy_usbram(ms_ep_tx, response, sizeof(response));
		btable_ep[USB_EP_MS_TX].tx_count = sizeof(response);
	} else if (state == USB_MS_SCSI_STATE_DATA_OUT)
		state = USB_MS_SCSI_STATE_REPLY;

	return scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

static void scsi_start_stop_unit(uint8_t *block, uint8_t in_len)
{
	state = USB_MS_SCSI_STATE_REPLY;

	/* terminate if fail to verify */
	if (scsi_verify_cdb6(block, in_len))
		return;

	/* do nothing */

	return scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

static void scsi_synchronize_cache10(uint8_t *block, uint8_t in_len)
{
	state = USB_MS_SCSI_STATE_REPLY;

	/* terminate if fail to verify */
	if (scsi_verify_cdb10(block, in_len))
		return;

	/* nothing to synchronize, return success */
	return scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

/*
 * Required by SPC-4.
 */
static void scsi_test_unit_ready(uint8_t *block, uint8_t in_len)
{
	state = USB_MS_SCSI_STATE_REPLY;

	/* terminate if fail to verify */
	if (scsi_verify_cdb6(block, in_len))
		return;

	if (spi_enable(1))
		return scsi_sense_code(SCSI_SENSE_NOT_READY,
			SCSI_SENSE_CODE_NOT_READY);

	return scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

/*
 * Required by SPC-4.
 */
static void scsi_write10(uint8_t *block, uint8_t in_len)
{
	int rv;
	int write_len;

	if (state == USB_MS_SCSI_STATE_PARSE) {
		state = USB_MS_SCSI_STATE_DATA_IN;

		/* terminate if fail to verify */
		if (scsi_verify_cdb10(block, in_len))
			return;

		/* RELADR bit not supported */
		if (block[1] & 0x1)
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_FIELD_IN_CDB);

		buffer = 0;
		offset = SCSI_BLOCK_SIZE_BYTES *
			(block[2] << 24 | block[3] << 16 |
				block[4] << 8 | block[5]);
		bytes = SCSI_BLOCK_SIZE_BYTES * (block[7] << 8 | block[8]);

		/* Chip has protection */
		if (spi_flash_check_protect(offset, bytes))
			return scsi_sense_code(SCSI_SENSE_DATA_PROTECT,
				SCSI_SENSE_CODE_WRITE_PROTECTED);

		/* Wait for any previous operation to complete */
		rv = spi_flash_wait();
		if (rv == EC_ERROR_TIMEOUT)
			return scsi_sense_code(
				SCSI_SENSE_HARDWARE_ERROR,
				SCSI_SENSE_CODE_TIMEOUT);

		rv = spi_flash_erase(offset, bytes);
		/* invalid address */
		if (rv == EC_ERROR_INVAL)
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_LBA_OUT_OF_RANGE);
		else if (rv != EC_SUCCESS)
			return scsi_sense_code(SCSI_SENSE_HARDWARE_ERROR,
				SCSI_SENSE_CODE_UNRECOVERED_READ_ERROR);
	} else if (state == USB_MS_SCSI_STATE_DATA_IN) {
		/* write whatever was received */
		write_len = MIN(bytes,
			btable_ep[USB_EP_MS_RX].rx_count & 0x3ff);
		ASSERT(write_len <= SPI_FLASH_MAX_WRITE_SIZE);

#if CONFIG_USB_MS_BUFFER_SIZE != USB_MS_PACKET_SIZE
		/* perform write only when local buffer is over full */
		if (buffer + write_len > CONFIG_USB_MS_BUFFER_SIZE) {
			/* Wait for previous operation to complete */
			rv = spi_flash_wait();
			if (rv == EC_ERROR_TIMEOUT)
				return scsi_sense_code(
					SCSI_SENSE_HARDWARE_ERROR,
					SCSI_SENSE_CODE_TIMEOUT);

			rv = spi_flash_write(offset,
				CONFIG_USB_MS_BUFFER_SIZE, temp_buf);
			if (rv == EC_ERROR_INVAL)
				return scsi_sense_code(
					SCSI_SENSE_ILLEGAL_REQUEST,
					SCSI_SENSE_CODE_LBA_OUT_OF_RANGE);
			else if (rv != EC_SUCCESS)
				return scsi_sense_code(
					SCSI_SENSE_HARDWARE_ERROR,
					SCSI_SENSE_CODE_UNRECOVERED_READ_ERROR);

			offset += buffer;
			bytes -= buffer;

			buffer = 0;
		}

		/* copy data to local buffer */
		memcpy(temp_buf + buffer, (uint8_t *) ms_ep_rx, write_len);
		buffer += write_len;

		/* on last write */
		if (bytes == buffer) {
			/* Wait for previous operation to complete */
			rv = spi_flash_wait();
			if (rv == EC_ERROR_TIMEOUT)
				return scsi_sense_code(
					SCSI_SENSE_HARDWARE_ERROR,
					SCSI_SENSE_CODE_TIMEOUT);

			rv = spi_flash_write(offset, buffer, temp_buf);
			if (rv == EC_ERROR_INVAL)
				return scsi_sense_code(
					SCSI_SENSE_ILLEGAL_REQUEST,
					SCSI_SENSE_CODE_LBA_OUT_OF_RANGE);
			else if (rv == EC_ERROR_ACCESS_DENIED)
				return scsi_sense_code(SCSI_SENSE_DATA_PROTECT,
					SCSI_SENSE_CODE_WRITE_PROTECTED);
			else if (rv != EC_SUCCESS)
				return scsi_sense_code(
					SCSI_SENSE_HARDWARE_ERROR,
					SCSI_SENSE_CODE_UNRECOVERED_READ_ERROR);

			/* Wait for last write to complete */
			rv = spi_flash_wait();
			if (rv == EC_ERROR_TIMEOUT)
				return scsi_sense_code(
					SCSI_SENSE_HARDWARE_ERROR,
					SCSI_SENSE_CODE_TIMEOUT);

			offset += buffer;
			bytes -= buffer;

			buffer = 0;
		/* received too much data */
		} else if (bytes < buffer)
			return scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_LBA_OUT_OF_RANGE);
#else
		memcpy(temp_buf, (uint8_t *) ms_ep_rx, write_len);

		/* Wait for previous operation to complete */
		rv = spi_flash_wait();
		if (rv == EC_ERROR_TIMEOUT)
			return scsi_sense_code(
				SCSI_SENSE_HARDWARE_ERROR,
				SCSI_SENSE_CODE_TIMEOUT);

		rv = spi_flash_write(offset, write_len, temp_buf);
		if (rv == EC_ERROR_INVAL)
			return scsi_sense_code(
				SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_LBA_OUT_OF_RANGE);
		else if (rv == EC_ERROR_ACCESS_DENIED)
			return scsi_sense_code(SCSI_SENSE_DATA_PROTECT,
				SCSI_SENSE_CODE_WRITE_PROTECTED);
		else if (rv != EC_SUCCESS)
			return scsi_sense_code(
				SCSI_SENSE_HARDWARE_ERROR,
				SCSI_SENSE_CODE_UNRECOVERED_READ_ERROR);

		offset += write_len;
		bytes -= write_len;
#endif

		/* nothing left to write */
		if (!bytes)
			state = USB_MS_SCSI_STATE_REPLY;
	}
	return scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

void scsi_reset(void)
{
	op = 0;

	offset = 0;
	bytes = 0;
	buffer = 0;

	state = USB_MS_SCSI_STATE_IDLE;
	/* set status to success by default */
	scsi_sense_code(SCSI_SENSE_NO_SENSE, SCSI_SENSE_CODE_NONE);
}

int scsi_parse(uint8_t *block, uint8_t in_len)
{
	/* set new operation */
	if (state == USB_MS_SCSI_STATE_IDLE) {
		state = USB_MS_SCSI_STATE_PARSE;

		op = block[0];
	}

	/* skip operation if sending reply */
	if (state != USB_MS_SCSI_STATE_REPLY) {
		switch (op) {
		case SCSI_INQUIRY:
			scsi_inquiry(block, in_len);
		break;
		case SCSI_MODE_SENSE6:
			scsi_mode_sense6(block, in_len);
		break;
		case SCSI_READ10:
			scsi_read10(block, in_len);
		break;
		case SCSI_READ_CAPACITY10:
			scsi_read_capacity10(block, in_len);
		break;
		case SCSI_READ_FORMAT_CAPACITIES:
			scsi_read_format_capacities(block, in_len);
		break;
		case SCSI_REPORT_LUNS:
			scsi_report_luns(block, in_len);
		break;
		case SCSI_REQUEST_SENSE:
			scsi_request_sense(block, in_len);
		break;
		case SCSI_START_STOP_UNIT:
			scsi_start_stop_unit(block, in_len);
		break;
		case SCSI_SYNCHRONIZE_CACHE10:
			scsi_synchronize_cache10(block, in_len);
		break;
		case SCSI_TEST_UNIT_READY:
			scsi_test_unit_ready(block, in_len);
		break;
		case SCSI_WRITE10:
			scsi_write10(block, in_len);
		break;
		default:
			state = USB_MS_SCSI_STATE_REPLY;
			scsi_sense_code(SCSI_SENSE_ILLEGAL_REQUEST,
				SCSI_SENSE_CODE_INVALID_COMMAND_OPERATION_CODE);
		break;
		}
	}

	/* error during data rx/tx */
	if (((state == USB_MS_SCSI_STATE_DATA_OUT) ||
			(state == USB_MS_SCSI_STATE_DATA_IN)) &&
			scsi_sense_data.key) {
		btable_ep[USB_EP_MS_TX].tx_count = 0;
		state = USB_MS_SCSI_STATE_REPLY;
		return SCSI_STATUS_CONTINUE;
	}

	/* done sending data */
	if (state == USB_MS_SCSI_STATE_REPLY) {
		state = USB_MS_SCSI_STATE_IDLE;
		return scsi_sense_data.key;
	}

	/* still sending/receiving data and no error has occurred */
	return SCSI_STATUS_CONTINUE;
}
