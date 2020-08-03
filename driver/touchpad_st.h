/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TOUCHPAD_ST_H
#define __CROS_EC_TOUCHPAD_ST_H

#include <stdint.h>

#include "common.h"

#define ST_VENDOR_ID		0x0483

#define ST_TP_EXTRA_BYTE	1

#define ST_TP_CMD_READ_ALL_EVENTS		0x87
#define ST_TP_CMD_WRITE_SCAN_MODE_SELECT	0xA0
#define ST_TP_CMD_WRITE_FEATURE_SELECT		0xA2
#define ST_TP_CMD_WRITE_SYSTEM_COMMAND		0xA4
#define ST_TP_CMD_WRITE_HOST_DATA_MEMORY	0xA6
#define ST_TP_CMD_READ_HOST_DATA_MEMORY		0xA7
#define ST_TP_CMD_WRITE_FW_CONFIG		0xA8
#define ST_TP_CMD_READ_FW_CONFIG		0xA9
#define ST_TP_CMD_SPI_HOST_BUFFER_ACK		0xC0
#define ST_TP_CMD_READ_SPI_HOST_BUFFER		0xC1

#define ST_TP_CMD_WRITE_HW_REG			0xFA
#define ST_TP_CMD_READ_HW_REG			0xFB

/* Max number of bytes that the DMA can burn on the flash in one shot in FTI */
#define ST_TP_FLASH_BUFFER_SIZE		(64 * 1024)
/* Max number of bytes that can be written in I2C to the DMA */
#define ST_TP_DMA_CHUNK_SIZE		32

#define ST_HOST_BUFFER_DATA_VALID	BIT(0)
#define ST_HOST_BUFFER_MT_READY		BIT(3)
#define ST_HOST_BUFFER_SF_READY		BIT(4)
#define ST_HOST_BUFFER_SS_READY		BIT(5)

#define ST_TP_SCAN_MODE_ACTIVE		0x00
#define ST_TP_SCAN_MODE_LOW_POWER	0x01
#define ST_TP_SCAN_MODE_TUNING_WIZARD	0x02
#define ST_TP_SCAN_MODE_LOCKED		0x03

#define ST_TOUCH_ROWS		(18)  /* force len */
#define ST_TOUCH_COLS		(25)  /* sense len */

#define ST_TOUCH_HEADER_SIZE	32

#define BYTES_PER_PIXEL		1
/* Number of bits per pixel, this value is decided by experiments. */
#define BITS_PER_PIXEL		8

#define ST_TOUCH_FRAME_SIZE	(ST_TOUCH_ROWS * ST_TOUCH_COLS * \
				 BYTES_PER_PIXEL)
#define ST_TOUCH_FORCE_SIZE	(ST_TOUCH_ROWS * BYTES_PER_PIXEL)
#define ST_TOUCH_SENSE_SIZE	(ST_TOUCH_COLS * BYTES_PER_PIXEL)

#define ST_TP_MEM_ID_SYSTEM_INFO	0x01

#define ST_TP_FLASH_OFFSET_CODE		(0x0000 << 2)
#define ST_TP_FLASH_OFFSET_PANEL_CFG	(0x6800 << 2)
#define ST_TP_FLASH_OFFSET_CX		(0x7000 << 2)
#define ST_TP_FLASH_OFFSET_CONFIG	(0x7C00 << 2)


struct st_tp_host_data_header_t {
#define ST_TP_HEADER_MAGIC	0xA5
	uint8_t magic;  /* this should always be ST_TP_HEADER_MAGIC */
	uint8_t host_data_mem_id;
	uint16_t count;
} __packed;

/* Compute offset of end of a member in given type */
#define endof(type, member) (offsetof(type, member) + \
			     sizeof(((type *)NULL)->member))

struct st_tp_system_info_t {
	/* Part 1, basic info */
	struct st_tp_host_data_header_t header;
	uint16_t api_ver_rev;
	uint8_t api_ver_minor;
	uint8_t api_ver_major;
	uint16_t chip0_ver;
	uint8_t chip0_id[2];  /* should be 0x3936 */
	uint16_t chip1_ver;
	uint16_t chip1_id;
	uint16_t fw_ver;
	uint16_t svn_rev;
	uint16_t cfg_ver;
	uint16_t cfg_project_id;
	uint16_t cx_ver;
	uint16_t cx_project_id;
	uint8_t cfg_afe_ver;
	uint8_t cx_afe_ver;
	uint8_t panel_cfg_afe_ver;
	uint8_t protocol;
	uint8_t die_id[16];
	uint64_t release_info; /* unsigned little endian 64 bit integer */
	uint32_t fw_crc;
	uint32_t cfg_crc;
#define ST_TP_SYSTEM_INFO_PART_1_SIZE endof(struct st_tp_system_info_t, cfg_crc)
#define ST_TP_SYSTEM_INFO_PART_1_RESERVED	16

	uint16_t scr_res_x;
	uint16_t scr_res_y;
	uint8_t scr_tx_len;
	uint8_t scr_rx_len;
	uint8_t key_len;
	uint8_t frc_len;
#define ST_TP_SYSTEM_INFO_PART_2_SIZE (endof(struct st_tp_system_info_t, \
					     frc_len) - \
				       offsetof(struct st_tp_system_info_t, \
						scr_res_x))
#define ST_TP_SYSTEM_INFO_PART_2_RESERVED	40

#if 0  /* the following parts are defined in spec, but not currently used. */

	uint16_t dbg_frame_addr;
#define ST_TP_SYSTEM_INFO_PART_3_SIZE (endof(struct st_tp_system_info_t, \
					     dbg_frame_addr) - \
				       offsetof(struct st_tp_system_info_t, \
						dbg_frame_addr))
#define ST_TP_SYSTEM_INFO_PART_3_RESERVED	6

	uint16_t ms_scr_raw_addr;
	uint16_t ms_scr_filter_addr;
	uint16_t ms_scr_str_addr;
	uint16_t ms_scr_bl_addr;
	uint16_t ss_tch_tx_raw_addr;
	uint16_t ss_tch_tx_filter_addr;
	uint16_t ss_tch_tx_str_addr;
	uint16_t ss_tch_tx_bl_addr;
	uint16_t ss_tch_rx_raw_addr;
	uint16_t ss_tch_rx_filter_addr;
	uint16_t ss_tch_rx_str_addr;
	uint16_t ss_tch_rx_bl_addr;
	uint16_t key_raw_addr;
	uint16_t key_filter_addr;
	uint16_t key_str_addr;
	uint16_t key_bl_addr;
	uint16_t frc_raw_addr;
	uint16_t frc_filter_addr;
	uint16_t frc_str_addr;
	uint16_t frc_bl_addr;
	uint16_t ss_hvr_tx_raw_addr;
	uint16_t ss_hvr_tx_filter_addr;
	uint16_t ss_hvr_tx_str_addr;
	uint16_t ss_hvr_tx_bl_addr;
	uint16_t ss_hvr_rx_raw_addr;
	uint16_t ss_hvr_rx_filter_addr;
	uint16_t ss_hvr_rx_str_addr;
	uint16_t ss_hvr_rx_bl_addr;
	uint16_t ss_prx_tx_raw_addr;
	uint16_t ss_prx_tx_filter_addr;
	uint16_t ss_prx_tx_str_addr;
	uint16_t ss_prx_tx_bl_addr;
	uint16_t ss_prx_rx_raw_addr;
	uint16_t ss_prx_rx_filter_addr;
	uint16_t ss_prx_rx_str_addr;
	uint16_t ss_prx_rx_bl_addr;
#define ST_TP_SYSTEM_INFO_PART_4_SIZE (endof(struct st_tp_system_info_t, \
					     ss_prx_rx_bl_addr) - \
				       offsetof(struct st_tp_system_info_t, \
						ms_scr_raw_addr))
#endif  /* if 0 */
} __packed;

#define ST_TP_SYSTEM_INFO_LEN	(sizeof(struct st_tp_system_info_t) + \
				 ST_TP_SYSTEM_INFO_PART_1_RESERVED)

struct st_tp_host_buffer_header_t {
#define ST_TP_BUFFER_HEADER_DATA_VALID		BIT(0)
#define ST_TP_BUFFER_HEADER_EVT_FIFO_NOT_EMPTY	BIT(1)
#define ST_TP_BUFFER_HEADER_SYS_FAULT		BIT(2)
#define ST_TP_BUFFER_HEADER_HEAT_MAP_MT_RDY	BIT(3)
#define ST_TP_BUFFER_HEADER_HEAT_MAP_SF_RDY	BIT(4)
#define ST_TP_BUFFER_HEADER_HEAT_MAP_SS_RDY	BIT(5)
#define ST_TP_BUFFER_HEADER_DOMESWITCH_LVL	BIT(6)
	uint8_t flags;
	uint8_t reserved[3];
	uint8_t heatmap_miss_count;
	uint8_t event_count;
	uint8_t event_miss_count;
} __packed;

struct st_tp_host_buffer_heat_map_t {
	uint8_t frame[ST_TOUCH_FRAME_SIZE];
#if 0  /* we are not using these now */
	uint8_t force[ST_TOUCH_FORCE_SIZE];
	uint8_t sense[ST_TOUCH_SENSE_SIZE];
#endif
} __packed;

struct st_tp_event_t {
#define ST_TP_EVENT_MAGIC		0x3
	unsigned magic:2;  /* should always be 0x3 */
	unsigned major_high:2;
#define ST_TP_EVENT_ID_CONTROLLER_READY	0x0
#define ST_TP_EVENT_ID_ENTER_POINTER	0x1
#define ST_TP_EVENT_ID_MOTION_POINTER	0x2
#define ST_TP_EVENT_ID_LEAVE_POINTER	0x3
#define ST_TP_EVENT_ID_STATUS_REPORT	0x4
#define ST_TP_EVENT_ID_USER_REPORT	0x5
#define ST_TP_EVENT_ID_DEBUG_REPORT	0xe
#define ST_TP_EVENT_ID_ERROR_REPORT	0xf
	unsigned evt_id:4;

	union {
		struct {
#define ST_TP_TOUCH_TYPE_INVALID	0x0
#define ST_TP_TOUCH_TYPE_FINGER		0x1
#define ST_TP_TOUCH_TYPE_GLOVE		0x2
#define ST_TP_TOUCH_TYPE_STYLUS		0x3
#define ST_TP_TOUCH_TYPE_PALM		0x4
			unsigned touch_type:4;
			unsigned touch_id:4;
			unsigned y:12;
			unsigned x:12;
			uint8_t z;
			uint8_t minor:4;  // need to be concat with minor_high
			uint8_t major:4;  // need to be concat with major_high
		} __packed finger;

		struct {
#define ST_TP_STATUS_CMD_ECHO	0x1
#define ST_TP_STATUS_FRAME_DROP	0x3
#define ST_TP_STATUS_FCAL	0x5
#define ST_TP_STATUS_BEACON	0x9
			uint8_t report_type;
			uint8_t info[4];
			uint8_t reserved;
		} __packed report;
	} __packed ; /* anonymous */

	unsigned minor_high:2;
	unsigned reserved:1;
	unsigned evt_left:5;
} __packed;

struct st_tp_fw_header_t {
	uint32_t signature;
	uint32_t ftb_ver;
	uint32_t chip_id;
	uint32_t svn_ver;
	uint32_t fw_ver;
	uint32_t config_id;
	uint32_t config_ver;
	uint8_t reserved[8];
	uint64_t release_info;
	uint32_t sec_size[4];
	uint32_t crc;
} __packed;

enum ST_TP_MODE {
	X_Y_MODE = 0,
	HEAT_MAP_MODE,
};

#define ST_TP_DEBUG_CMD_RESET_TOUCHPAD		0x00
#define ST_TP_DEBUG_CMD_CALIBRATE		0x01
#define ST_TP_DEBUG_CMD_START_SCAN		0x02
#define ST_TP_DEBUG_CMD_STOP_SCAN		0x03
#define ST_TP_DEBUG_CMD_READ_BUF_HEADER		0x04
#define ST_TP_DEBUG_CMD_READ_EVENTS		0x05

#define ST_TP_HEAT_MAP_THRESHOLD 10

/* A minimum version that supports heatmap mode. */
#define ST_TP_MIN_HEATMAP_VERSION 0x12

#endif /* __CROS_EC_TOUCHPAD_ST_H */

