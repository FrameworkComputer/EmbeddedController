/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __SMHI_CLIENT_H
#define __SMHI_CLIENT_H

#include "heci.h"

#include <string.h>

#include <zephyr/kernel.h>

#define REBOOT_FLAG BIT(0)
#define SMHI_CONN_FLAG BIT(1)
#define MAX_DMA_DEV (3)
#define DMA_CHAN_PER_DEV (8)
#define DMA_CHAN_NUM (MAX_DMA_DEV * DMA_CHAN_PER_DEV)

#define SMHI_MAJOR_VERSION 0
#define SMHI_MINOR_VERSION 1
#define SMHI_HOTFIX_VERSION 2
#define SMHI_BUILD_VERSION 3

/* SMHI Commands */
enum smhi_command_id {
	/*retrieve system info*/
	SMHI_GET_VERSION = 0x1,
	SMHI_GET_TIME = 0x8,
	SMHI_GET_VNN_STATUS = 0x21,
	SMHI_GET_DMA_USAGE = 0x22,
	SMHI_GET_STAT = 0x70,
	/* System control*/
	SMHI_FW_RESET = 0x2,
	SMHI_COMMAND_LAST
};

struct smhi_msg_hdr_t {
	uint8_t command : 7;
	uint8_t is_response : 1;
	uint16_t has_next : 1;
	uint16_t reserved : 15;
	uint8_t status;
} __packed;

struct smhi_get_version_resp {
	uint16_t major;
	uint16_t minor;
	uint16_t hotfix;
	uint16_t build;
} __packed;

#endif /* __SMHI_CLIENT_H */
