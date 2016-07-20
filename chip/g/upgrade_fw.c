/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "console.h"
#include "dcrypto/dcrypto.h"
#include "extension.h"
#include "flash.h"
#include "hooks.h"
#include "include/compile_time_macros.h"
#include "memory.h"
#include "uart.h"

#include "cryptoc/sha.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

/* Various upgrade extension command return values. */
enum return_value {
	UPGRADE_SUCCESS = 0,
	UPGRADE_BAD_ADDR = 1,
	UPGRADE_ERASE_FAILURE = 2,
	UPGRADE_DATA_ERROR = 3,
	UPGRADE_WRITE_FAILURE = 4,
	UPGRADE_VERIFY_ERROR = 5,
	UPGRADE_GEN_ERROR = 6,
};

/*
 * The payload of the upgrade command. (Integer values in network byte order).
 *
 * block digest: the first four bytes of the sha1 digest of the rest of the
 *               structure.
 * block_base:  address where this block needs to be written to.
 * block_body:  variable size data to written at address 'block_base'.
 */
struct upgrade_command {
	uint32_t  block_digest;
	uint32_t  block_base;
	uint8_t   block_body[0];
} __packed;

/*
 * This array defines two possibe sections available for the firmare update.
 * The section whcih does not map the current execting code is picked as the
 * valid update area. The values are offsets into the flash space.
 */
const struct section_descriptor {
	uint32_t sect_base_offset;
	uint32_t sect_top_offset;
} rw_sections[] = {
	{CONFIG_RW_MEM_OFF,
	 CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE},
	{CONFIG_RW_B_MEM_OFF,
	 CONFIG_RW_B_MEM_OFF + CONFIG_RW_SIZE}
};

const struct section_descriptor *valid_section;

/* Pick the section where updates can go to based on current code address. */
static void set_valid_section(void)
{
	int i;
	uint32_t run_time_offs = (uint32_t) set_valid_section -
		CONFIG_PROGRAM_MEMORY_BASE;

	for (i = 0; i < ARRAY_SIZE(rw_sections); i++) {
		if ((run_time_offs > rw_sections[i].sect_base_offset) &&
		    (run_time_offs < rw_sections[i].sect_top_offset))
			continue;
		valid_section = rw_sections + i;
		break;
	}
}

/* Verify that the passed in block fits into the valid area. */
static int valid_upgrade_chunk(uint32_t block_offset, size_t body_size)
{
	if (valid_section &&
	    (block_offset >= valid_section->sect_base_offset) &&
	    ((block_offset + body_size) < valid_section->sect_top_offset))
		return 1;

	return 0;
}

void fw_upgrade_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size)
{
	struct upgrade_command *cmd_body = body;
	uint8_t *rv = body;
	uint8_t sha1_digest[SHA_DIGEST_SIZE];
	size_t body_size;
	uint32_t block_offset;

	/*
	 * A single byte response, unless this is the first message in the
	 * programming sequence.
	 */
	*response_size = sizeof(*rv);

	if (cmd_size < offsetof(struct upgrade_command, block_body)) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		*rv = UPGRADE_GEN_ERROR;
		return;
	}
	body_size = cmd_size - offsetof(struct upgrade_command, block_body);

	if (!cmd_body->block_base && !body_size) {
		/*
		 * This is the first message of the upgrade process, let's
		 * determine the valid upgrade section and erase its contents.
		 */
		set_valid_section();

		if (flash_physical_erase(valid_section->sect_base_offset,
					 valid_section->sect_top_offset -
					 valid_section->sect_base_offset)) {
			CPRINTF("%s:%d erase failure of 0x%x..+0x%x\n",
				__func__, __LINE__,
				valid_section->sect_base_offset,
				valid_section->sect_top_offset -
				valid_section->sect_base_offset);
			*rv = UPGRADE_ERASE_FAILURE;
			return;
		}

		/*
		  crosbug.com/p/54916
		  wipe_nvram(); Do not keep any state around.
		*/

		/*
		 * Successful erase means that we need to return the base
		 * address of the section to be programmed with the upgrade.
		 */
		*(uint32_t *)body = htobe32(valid_section->sect_base_offset +
					    CONFIG_PROGRAM_MEMORY_BASE);
		*response_size = sizeof(uint32_t);
		return;
	}

	/* Check if the block will fit into the valid area. */
	block_offset = be32toh(cmd_body->block_base) -
		CONFIG_PROGRAM_MEMORY_BASE;
	if (!valid_upgrade_chunk(block_offset, body_size)) {
		*rv = UPGRADE_BAD_ADDR;
		CPRINTF("%s:%d %x, %d base %x top %x\n", __func__, __LINE__,
			block_offset, body_size,
			valid_section->sect_base_offset,
			valid_section->sect_top_offset);
		return;
	}

	/* Check if the block was received properly. */
	DCRYPTO_SHA1_hash((uint8_t *)&cmd_body->block_base,
			  body_size + sizeof(cmd_body->block_base),
			  sha1_digest);
	if (memcmp(sha1_digest, &cmd_body->block_digest,
		   sizeof(cmd_body->block_digest))) {
		*rv = UPGRADE_DATA_ERROR;
		CPRINTF("%s:%d sha1 %x not equal received %x at offs. 0x%x\n",
			__func__, __LINE__,
			*(uint32_t *)sha1_digest, cmd_body->block_digest,
			block_offset);
		return;
	}

	CPRINTF("%s: programming at address 0x%x\n", __func__,
		block_offset + CONFIG_PROGRAM_MEMORY_BASE);
	if (flash_physical_write(block_offset, body_size,
				 cmd_body->block_body) != EC_SUCCESS) {
		*rv = UPGRADE_WRITE_FAILURE;
		CPRINTF("%s:%d upgrade write error\n",
			__func__, __LINE__);
		return;
	}

	/* Werify that data was written properly. */
	if (memcmp(cmd_body->block_body, (void *)
		   (block_offset + CONFIG_PROGRAM_MEMORY_BASE),
		   body_size)) {
		*rv = UPGRADE_VERIFY_ERROR;
		CPRINTF("%s:%d upgrade verification error\n",
			__func__, __LINE__);
		return;
	}

	*rv = UPGRADE_SUCCESS;
}
