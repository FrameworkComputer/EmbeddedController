/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Internal header with RTS54xx interface constants and types */

#ifndef __CROS_EC_PDC_RTS54XX_H
#define __CROS_EC_PDC_RTS54XX_H

#include "drivers/pdc.h"

#include <string.h>

/**
 * @brief RTS54XX I2C block read command
 */
#define RTS54XX_BLOCK_READ_CMD 0x80

/**
 * @brief Offsets of data fields in the GET_IC_STATUS response
 *
 * These are based on the Realtek spec version 3.3.25.
 *
 * "Data Byte 0" is the first byte after "Byte Count" and is available
 * at .rd_buf[1].
 */
#define RTS54XX_GET_IC_STATUS_RUNNING_FLASH_CODE 1
#define RTS54XX_GET_IC_STATUS_FWVER_MAJOR_OFFSET 4
#define RTS54XX_GET_IC_STATUS_FWVER_MINOR_OFFSET 5
#define RTS54XX_GET_IC_STATUS_FWVER_PATCH_OFFSET 6
#define RTS54XX_GET_IC_STATUS_VID_L 10
#define RTS54XX_GET_IC_STATUS_VID_H 11
#define RTS54XX_GET_IC_STATUS_PID_L 12
#define RTS54XX_GET_IC_STATUS_PID_H 13
#define RTS54XX_GET_IC_STATUS_RUNNING_FLASH_BANK 15
#define RTS54XX_GET_IC_STATUS_PD_REV_MAJOR_OFFSET 23
#define RTS54XX_GET_IC_STATUS_PD_REV_MINOR_OFFSET 24
#define RTS54XX_GET_IC_STATUS_PD_VER_MAJOR_OFFSET 25
#define RTS54XX_GET_IC_STATUS_PD_VER_MINOR_OFFSET 26
#define RTS54XX_GET_IC_STATUS_PROG_NAME_STR 27
#define RTS54XX_GET_IC_STATUS_PROG_NAME_STR_LEN 12
#define RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_OFFSET 39

/** Number of GET_IC_STATUS bytes that can be requested while running in ROM
 *  code. ROM code does not support the full-length response.
 *
 *  Do not use as a receive buffer size because the RTK response includes an
 *  extra length byte at response offset 0. Use RTS54XX_GET_IC_STATUS_RX_BUF_LEN
 */
#define RTS54XX_GET_IC_STATUS_SAFE_READ_LEN 20

/** Number of GET_IC_STATUS bytes that can be requested while running flash
 *  FW >= 0.3.0 that includes the project name string (all modern RTK PDC FW).
 *  Used by rts54_get_info() for general chip info queries.
 *
 *  This skips the SBU mux mode byte at response offset 39, because not all FW
 *  in circulation supports that. SBU mux mode is only queried by
 *  rts54_get_sbu_mux_mode() when this feature is supported (per Kconfig).
 *
 *  Do not use as a receive buffer size because the RTK response includes an
 *  extra length byte at response offset 0. Use RTS54XX_GET_IC_STATUS_RX_BUF_LEN
 */
#define RTS54XX_GET_IC_STATUS_FULL_READ_LEN 38

/** Safe RX buffer size for a GET_IC_STATUS response, including the length byte
 *  at response offset 0, and sized for the maximum possible response payload
 *  length, which includes the SBU mux mode byte at response offset 39.
 */
#define RTS54XX_GET_IC_STATUS_RX_BUF_LEN \
	(RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_OFFSET + 1)

static inline void
rts54xx_unpack_get_ic_status_response(uint8_t *rx_buf, struct pdc_info_t *info)
{
	/* Realtek Is running flash code: Data Byte0 */
	info->is_running_flash_code =
		rx_buf[RTS54XX_GET_IC_STATUS_RUNNING_FLASH_CODE];

	/* Realtek FW main version: Data Byte3..5 */
	info->fw_version =
		rx_buf[RTS54XX_GET_IC_STATUS_FWVER_MAJOR_OFFSET] << 16 |
		rx_buf[RTS54XX_GET_IC_STATUS_FWVER_MINOR_OFFSET] << 8 |
		rx_buf[RTS54XX_GET_IC_STATUS_FWVER_PATCH_OFFSET];

	/* Realtek VID: Data Byte9..10 (little-endian) */
	info->vid = rx_buf[RTS54XX_GET_IC_STATUS_VID_H] << 8 |
		    rx_buf[RTS54XX_GET_IC_STATUS_VID_L];

	/* Realtek PID: Data Byte11..12 (little-endian) */
	info->pid = rx_buf[RTS54XX_GET_IC_STATUS_PID_H] << 8 |
		    rx_buf[RTS54XX_GET_IC_STATUS_PID_L];

	/* Realtek Running flash bank offset: Data Byte14 */
	info->running_in_flash_bank =
		rx_buf[RTS54XX_GET_IC_STATUS_RUNNING_FLASH_BANK];

	/* Realtek PD Revision: Data Byte22..23 (big-endian) */
	info->pd_revision = rx_buf[RTS54XX_GET_IC_STATUS_PD_REV_MAJOR_OFFSET]
				    << 8 |
			    rx_buf[RTS54XX_GET_IC_STATUS_PD_REV_MINOR_OFFSET];

	/* Realtek PD Version: Data Byte24..25 (big-endian) */
	info->pd_version = rx_buf[RTS54XX_GET_IC_STATUS_PD_VER_MAJOR_OFFSET]
				   << 8 |
			   rx_buf[RTS54XX_GET_IC_STATUS_PD_VER_MINOR_OFFSET];
}

/* FW project name length should not exceed the max length supported in struct
 * pdc_info_t
 */
BUILD_ASSERT(RTS54XX_GET_IC_STATUS_PROG_NAME_STR_LEN <=
	     (sizeof(((struct pdc_info_t *)0)->project_name) - 1));

#define RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_NORMAL 0
#define RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_FORCE_DBG 1

// Used for SET_VDO command
#define RTS54XX_PDC_ORIGIN 0
#define RTS54XX_SET_VDO_MAX_VDOS 5
#define RTS54XX_VDO_TYPE_AND_VALUE_SIZE 5
#define RTS54XX_SET_VDO_HEADER_SIZE 5
#define RTS54XX_SET_VDO_MSG_SIZE(x) \
	RTS54XX_SET_VDO_HEADER_SIZE + (x * RTS54XX_VDO_TYPE_AND_VALUE_SIZE)

/**
 * @brief PDC Command states
 */
enum cmd_sts_t {
	/** Command has not been started */
	CMD_BUSY = 0,
	/** Command has completed */
	CMD_DONE = 1,
	/** Command has been started but has not completed */
	CMD_DEFERRED = 2,
	/** Command completed with error. Send GET_ERROR_STATUS for details */
	CMD_ERROR = 3
};

/**
 * @brief Ping Status of the PDC
 */
union ping_status_t {
	struct {
		/** Command status */
		uint8_t cmd_sts : 2;
		/** Length of data read to read */
		uint8_t data_len : 6;
	};
	uint8_t raw_value;
};

/**
 * @brief VDO configuration
 *
 * This union is used to configure the VDO
 */
typedef union {
	struct {
		uint8_t num_vdos : 3; // Bits [2:0]
		uint8_t origin : 1; // Bit  [3]
		uint8_t reserved : 4; // Bits [7:4]
	} __attribute__((packed)) fields;

	uint8_t raw;
} vdo_config_t;

/**
 * @brief Sx sleep state values
 *
 * The value used to indicate to the PDC the current AP power state
 */
enum sx_sleep_state {
	SX_RSVD = 0,
	SX_S0 = 1,
	SX_S3 = 2,
	SX_S5 = 3,
	SX_S4 = 4,
	SX_SOIX = 5,

};

#endif /* __CROS_EC_PDC_RTS54XX_H */
