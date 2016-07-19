/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UPDATE_FW_H
#define __CROS_EC_UPDATE_FW_H

#include <stddef.h>

/* TODO: Handle this in update_fw.c, not usb_update.c */
#define UPDATE_DONE          0xB007AB1E

/*
 * This array defines possible sections available for the firmare update.
 * The section which does not map the current execting code is picked as the
 * valid update area. The values are offsets into the flash space.
 *
 * This should be defined in board.c, with each entry containing:
 * {CONFIG_RW_MEM_OFF, CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE}
 * for its relevant section.
 */
struct section_descriptor {
	uint32_t sect_base_offset;
	uint32_t sect_top_offset;
};

extern const struct section_descriptor * const rw_sections;
extern const int num_rw_sections;


void fw_update_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size);

#endif  /* ! __CROS_EC_UPDATE_FW_H */
