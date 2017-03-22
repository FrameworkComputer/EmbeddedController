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

const struct section_descriptor *valid_section;

/*
 * Pick the section where updates can go to based on current code address.
 *
 * TODO(b/36375666): Each board/chip should be able to re-define this.
 */
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

/*
 * Verify that the passed in block fits into the valid area. If it does, and
 * is destined to the base address of the area - erase the area contents.
 *
 * Return success, or indication of an erase failure or chunk not fitting into
 * valid area.
 *
 * TODO(b/36375666): Each board/chip should be able to re-define this.
 */
static uint8_t check_update_chunk(uint32_t block_offset, size_t body_size)
{
	uint32_t base;
	uint32_t size;

	/* Is this an RW chunk? */
	if (valid_section &&
	    (block_offset >= valid_section->sect_base_offset) &&
	    ((block_offset + body_size) <= valid_section->sect_top_offset)) {

		base = valid_section->sect_base_offset;
		size = valid_section->sect_top_offset -
			 valid_section->sect_base_offset;
		/*
		 * If this is the first chunk for this section, it needs to
		 * be erased.
		 */
		if (block_offset == base) {
			if (flash_physical_erase(base, size) != EC_SUCCESS) {
				CPRINTF("%s:%d erase failure of 0x%x..+0x%x\n",
					__func__, __LINE__, base, size);
				return UPDATE_ERASE_FAILURE;
			}
		}

		return UPDATE_SUCCESS;
	}

	CPRINTF("%s:%d %x, %d section base %x top %x\n",
		__func__, __LINE__,
		block_offset, body_size,
		valid_section->sect_base_offset,
		valid_section->sect_top_offset);

	return UPDATE_BAD_ADDR;

}

/* TODO(b/36375666): These need to be overridden for chip/g. */
int update_pdu_valid(struct update_command *cmd_body, size_t cmd_size)
{
	return 1;
}

static int chunk_came_too_soon(uint32_t block_offset)
{
	return 0;
}

static void new_chunk_written(uint32_t block_offset)
{
}

static int contents_allowed(uint32_t block_offset,
			    size_t body_size, void *update_data)
{
	return 1;
}

/*
 * Setup internal state (e.g. valid sections, and fill first response).
 *
 * Assumes rpdu is already prefilled with 0, and that version has already
 * been set. May set a return_value != 0 on error.
 *
 * TODO(b/36375666): Each board/chip should be able to re-define this.
 */
void fw_update_start(struct first_response_pdu *rpdu)
{
	/* Determine the valid update section. */
	set_valid_section();

	/*
	 * If there have been any problems when determining the valid
	 * section offsets/sizes - return an error code.
	 */
	if (!valid_section) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		rpdu->return_value = htobe32(UPDATE_GEN_ERROR);
		return;
	}

	/*
	 * TODO(b/36375666): We reuse the same structure as cr50 updater, but
	 * there isn't a whole lot that can be shared... We should probably
	 * switch to a board-specific response packet (at least common vs
	 * cr50-specific).
	 */
	rpdu->backup_ro_offset = htobe32(valid_section->sect_base_offset);
	rpdu->backup_rw_offset = 0x0;

	/* RO header information. */
	rpdu->shv[0].minor = 0;
	rpdu->shv[0].major = 0;
	rpdu->shv[0].epoch = 0;
	rpdu->keyid[0] = 0;

	/* RW header information. */
	rpdu->shv[1].minor = 0;
	rpdu->shv[1].major = 0;
	rpdu->shv[1].epoch = 0;

	rpdu->keyid[1] = 0;
}

void fw_update_command_handler(void *body,
			       size_t cmd_size,
			       size_t *response_size)
{
	struct update_command *cmd_body = body;
	void *update_data;
	uint8_t *error_code = body;  /* Cache the address for code clarity. */
	size_t body_size;
	uint32_t block_offset;

	*response_size = 1; /* One byte response unless this is a start PDU. */

	if (cmd_size < sizeof(struct update_command)) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		*error_code = UPDATE_GEN_ERROR;
		return;
	}
	body_size = cmd_size - sizeof(struct update_command);

	if (!cmd_body->block_base && !body_size) {
		struct first_response_pdu *rpdu = body;

		/*
		 * This is the connection establishment request, the response
		 * allows the server to decide what sections of the image to
		 * send to program into the flash.
		 */

		/* First, prepare the response structure. */
		memset(rpdu, 0, sizeof(*rpdu));
		*response_size = sizeof(*rpdu);
		rpdu->protocol_version = htobe32(UPDATE_PROTOCOL_VERSION);

		/* Setup internal state (e.g. valid sections, and fill rpdu) */
		fw_update_start(rpdu);
		return;
	}

	block_offset = be32toh(cmd_body->block_base);

	if (!update_pdu_valid(cmd_body, cmd_size)) {
		*error_code = UPDATE_DATA_ERROR;
		return;
	}

	update_data = cmd_body + 1;
	if (!contents_allowed(block_offset, body_size, update_data)) {
		*error_code = UPDATE_ROLLBACK_ERROR;
		return;
	}

	/* Check if the block will fit into the valid area. */
	*error_code = check_update_chunk(block_offset, body_size);
	if (*error_code)
		return;

	if (chunk_came_too_soon(block_offset)) {
		*error_code = UPDATE_RATE_LIMIT_ERROR;
		return;
	}

	/*
	 * TODO(b/36375666): chip/g code has some cr50-specific stuff right
	 * here, which should probably be merged into contents_allowed...
	 */

	CPRINTF("update: 0x%x\n", block_offset + CONFIG_PROGRAM_MEMORY_BASE);
	if (flash_physical_write(block_offset, body_size, update_data)
	    != EC_SUCCESS) {
		*error_code = UPDATE_WRITE_FAILURE;
		CPRINTF("%s:%d update write error\n", __func__, __LINE__);
		return;
	}

	new_chunk_written(block_offset);

	/* Verify that data was written properly. */
	if (memcmp(update_data, (void *)
		   (block_offset + CONFIG_PROGRAM_MEMORY_BASE),
		   body_size)) {
		*error_code = UPDATE_VERIFY_ERROR;
		CPRINTF("%s:%d update verification error\n",
			__func__, __LINE__);
		return;
	}

	*error_code = UPDATE_SUCCESS;
}

/* TODO(b/36375666): This need to be overridden for chip/g. */
void fw_update_complete(void)
{
}
