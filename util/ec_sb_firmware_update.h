/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery Firmware Update driver.
 * Ref: Common Smart Battery System Interface Specification v8.0.
 *
 * cmd.0x35, Write Word
 *   0x1000: Prepare to Update
 *   0x2000: End of Update
 *   0xF000: Update Firmware
 *
 * cmd.0x35, Read Word
 *   Firmware Update Status
 *
 * cmd.0x36 Write Block
 *   Send 32 byte firmware image
 *
 * cmd.0x37 Read Word
 *   Get Battery Information
 *   sequence:=b1,b0,b3,b2,b5,b5,b7,b6
 *
 * Command Sequence for Battery FW Update
 *
 *  0. cmd.0x35.read
 *  1. cmd.0x37.read
 *  2. cmd.0x35.write.0x1000
 *  3. cmd.0x35.read.status (optional)
 *  4. cmd.0x35.write.0xF000
 *  5. cmd.0x35.read.status
 *     if bit8-0, go to step 2
 *  6. cmd.0x36.write.32byte
 *  7. cmd.0x35.read.status
 *     if FEC.b13=1, go to step 6
 *     if fatal.b12=1, go to step 2
 *     if b11,b10,b9,b2,b1,b0; go to step 1
 *     if b5,b3; go to step 8
 *    (repeat 6,7)
 *  8. cmd.0x36.write.0x2000
 *  9. cmd.0x35.read.status
 */

#ifndef __UTIL_EC_SB_FIRMWARE_UPDATE_H
#define __UTIL_EC_SB_FIRMWARE_UPDATE_H

struct sb_fw_header {
	uint8_t signature[4]; /* "BTFW" */
	uint16_t hdr_version; /* 0x0100 */
	uint16_t pkg_version_major_minor;

	uint16_t vendor_id; /* 8,9 */
	uint16_t battery_type; /* A B */

	uint16_t fw_version; /* C D */
	uint16_t data_table_version; /* E F */
	uint32_t fw_binary_offset; /*0x10 0x11 0x12 0x13 */
	uint32_t fw_binary_size; /* 0x14 0x15 0x16 0x17 */
	uint8_t checksum; /* 0x18 */
};

/**
 * sb.fw.update.cmd.0x35, Read Word
 *    Firmware Update Status
 */
struct sb_fw_update_status {
	uint16_t v_fail_maker_id : 1; /* b0 */
	uint16_t v_fail_hw_id : 1; /* b1 */
	uint16_t v_fail_fw_version : 1; /* b2 */
	uint16_t v_fail_permanent : 1; /* b3 */

	uint16_t rsvd5 : 1; /* b4 */
	uint16_t permanent_failure : 1; /* b5 */
	uint16_t abnormal_condition : 1; /* b6 */
	uint16_t fw_update_supported : 1; /* b7 */

	uint16_t fw_update_mode : 1; /* b8 */
	uint16_t fw_corrupted : 1; /* b9 */
	uint16_t cmd_reject : 1; /* b10 */
	uint16_t invalid_data : 1; /* b11 */

	uint16_t fw_fatal_error : 1; /* b12 */
	uint16_t fec_error : 1; /* b13 */
	uint16_t busy : 1; /* b14 */
	uint16_t rsvd15 : 1; /* b15 */
} __packed;

/**
 * sb.fw.update.cmd.0x37 Read Word
 *     Get Battery Information
 *     sequence:=b1,b0,b3,b2,b5,b5,b7,b6
 */
struct sb_fw_update_info {
	uint16_t maker_id; /* b0, b1 */
	uint16_t hardware_id; /* b2, b3 */
	uint16_t fw_version; /* b4, b5 */
	uint16_t data_version; /* b6, b7 */
} __packed;

/**
 * smart.battery.maker.id
 */
enum sb_maker_id {
	sb_maker_id_lgc = 0x0001, /* b0=0; b1=1 */
	sb_maker_id_panasonic = 0x0002,
	sb_maker_id_sanyo = 0x0003,
	sb_maker_id_sony = 0x0004,
	sb_maker_id_simplo = 0x0005,
	sb_maker_id_celxpert = 0x0006,
};

/*
 * Ref: Common Smart Battery System Interface Specification v8.0 page 21-24.
 * case 1. If permanent error:b5,b3, go to setp8.
 * case 2. If error:b11,b10,b9 b2,b1,b0, go to step1. Retry < 3 times.
 * case 3. If firmware update fatal error:b12, go to step2. Retry < 3 times.
 *         In order to simply the implementation, I merged case 2 and 3.
 *         If firmware update fatal error:b12, go to step1 as well.
 * case 4. If error.FEC.b13=1, go to step6. Retry < 3 times.
 * case 5. If battery interface is busy, retry < 10 times.
 *    Delay 1 second between retries.
 */
#define SB_FW_UPDATE_ERROR_RETRY_CNT 2
#define SB_FW_UPDATE_FEC_ERROR_RETRY_CNT 2
#define SB_FW_UPDATE_BUSY_ERROR_RETRY_CNT 4

#endif
