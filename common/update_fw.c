/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "console.h"
#include "extension.h"
#include "flash.h"
#include "hooks.h"
#include "include/compile_time_macros.h"
#include "uart.h"
#include "update_fw.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

/* Various update extension command return values. */
enum return_value {
	UPDATE_SUCCESS = 0,
	UPDATE_BAD_ADDR = 1,
	UPDATE_ERASE_FAILURE = 2,
	UPDATE_DATA_ERROR = 3,
	UPDATE_WRITE_FAILURE = 4,
	UPDATE_VERIFY_ERROR = 5,
	UPDATE_GEN_ERROR = 6,
};

/*
 * The payload of the update command. (Integer values in network byte order).
 *
 * block digest: the first four bytes of the sha1 digest of the rest of the
 *               structure.
 * block_base:  address where this block needs to be written to.
 * block_body:  variable size data to written at address 'block_base'.
 */
struct update_command {
	uint32_t  block_digest;
	uint32_t  block_base;
	uint8_t   block_body[0];
} __packed;


const struct section_descriptor *valid_section;

/* Pick the section where updates can go to based on current code address. */
static int set_valid_section(void)
{
	int i;
	uint32_t run_time_offs = (uint32_t) set_valid_section -
		CONFIG_PROGRAM_MEMORY_BASE;
	valid_section = rw_sections;

	for (i = 0; i < num_rw_sections; i++) {
		if ((run_time_offs > rw_sections[i].sect_base_offset) &&
		    (run_time_offs < rw_sections[i].sect_top_offset))
			continue;
		valid_section = rw_sections + i;
		break;
	}
	if (i == num_rw_sections) {
		CPRINTF("%s:%d No valid section found!\n", __func__, __LINE__);
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

/* Verify that the passed in block fits into the valid area. */
static int valid_update_chunk(uint32_t block_offset, size_t body_size)
{
	if (valid_section &&
	    (block_offset >= valid_section->sect_base_offset) &&
	    ((block_offset + body_size) <= valid_section->sect_top_offset))
		return 1;

	return 0;
}

void fw_update_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size)
{
	struct update_command *cmd_body = body;
	uint8_t *rv = body;
	size_t body_size;
	uint32_t block_offset;

	/*
	 * A single byte response, unless this is the first message in the
	 * programming sequence.
	 */
	*response_size = sizeof(*rv);

	body_size = cmd_size - offsetof(struct update_command, block_body);
	if (body_size < 0) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		*rv = UPDATE_GEN_ERROR;
		return;
	}

	if (!cmd_body->block_base && !body_size) {
		int ret;
		/*
		 * This is the first message of the update process, let's
		 * determine the valid update section and erase its contents.
		 */
		ret = set_valid_section();
		if (ret) {
			CPRINTF("%s:%d no valid section\n", __func__, __LINE__);
			return;
		}

		if (flash_physical_erase(valid_section->sect_base_offset,
					 valid_section->sect_top_offset -
					 valid_section->sect_base_offset)) {
			CPRINTF("%s:%d erase failure of 0x%x..+0x%x\n",
				__func__, __LINE__,
				valid_section->sect_base_offset,
				valid_section->sect_top_offset -
				valid_section->sect_base_offset);
			*rv = UPDATE_ERASE_FAILURE;
			return;
		}

		/*
		 * Successful erase means that we need to return the base
		 * address of the section to be programmed with the update.
		 */
		*(uint32_t *)body = htobe32(valid_section->sect_base_offset +
					    CONFIG_PROGRAM_MEMORY_BASE);
		*response_size = sizeof(uint32_t);
		return;
	}

	/* Check if the block will fit into the valid area. */
	block_offset = be32toh(cmd_body->block_base) -
		CONFIG_PROGRAM_MEMORY_BASE;
	if (!valid_update_chunk(block_offset, body_size)) {
		*rv = UPDATE_BAD_ADDR;
		CPRINTF("%s:%d Write out of range %x ..+%d (Window %x - %x)\n",
			__func__, __LINE__,
			block_offset, body_size,
			valid_section->sect_base_offset,
			valid_section->sect_top_offset);
		return;
	}

	if (flash_physical_write(block_offset, body_size,
				 cmd_body->block_body) != EC_SUCCESS) {
		*rv = UPDATE_WRITE_FAILURE;
		CPRINTF("%s:%d update write error @0x%x:%x\n",
			__func__, __LINE__, block_offset, body_size);
		return;
	}

	/* Werify that data was written properly. */
	if (memcmp(cmd_body->block_body, (void *)
		   (block_offset + CONFIG_PROGRAM_MEMORY_BASE),
		   body_size)) {
		*rv = UPDATE_VERIFY_ERROR;
		CPRINTF("%s:%d update verification error\n",
			__func__, __LINE__);
		return;
	}

	*rv = UPDATE_SUCCESS;
}
