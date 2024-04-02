/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/endian.h"
#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "include/compile_time_macros.h"
#include "rollback.h"
#include "rwsig.h"
#include "sha256.h"
#include "system.h"
#include "uart.h"
#include "update_fw.h"
#include "util.h"
#include "vb21_struct.h"
#include "vboot.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/devicetree.h>
#endif

#if defined(CONFIG_TOUCHPAD_VIRTUAL_OFF) && defined(CONFIG_TOUCHPAD_HASH_FW)
#define CONFIG_TOUCHPAD_FW_CHUNKS \
	(CONFIG_TOUCHPAD_VIRTUAL_SIZE / CONFIG_UPDATE_PDU_SIZE)

#ifdef CONFIG_ZEPHYR

const uint8_t __keep
	touchpad_fw_hashes[CONFIG_TOUCHPAD_FW_CHUNKS][SHA256_DIGEST_SIZE];
const uint8_t __keep touchpad_fw_full_hash[SHA256_DIGEST_SIZE];

/* Check if the fmap section size is correct */
BUILD_ASSERT(sizeof(touchpad_fw_hashes) ==
	     DT_PROP(DT_NODELABEL(touchpad_fw_hashes), size));
BUILD_ASSERT(sizeof(touchpad_fw_full_hash) ==
	     DT_PROP(DT_NODELABEL(touchpad_fw_full_hash), size));

#else
#include "touchpad_fw_hash.h"
#endif

BUILD_ASSERT(sizeof(touchpad_fw_hashes) ==
	     (CONFIG_TOUCHPAD_FW_CHUNKS * SHA256_DIGEST_SIZE));
BUILD_ASSERT(sizeof(touchpad_fw_hashes[0]) == SHA256_DIGEST_SIZE);

BUILD_ASSERT(sizeof(touchpad_fw_full_hash) == SHA256_DIGEST_SIZE);
#endif

#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)

/* Section to be updated (i.e. not the current section). */
struct {
	uint32_t base_offset;
	uint32_t top_offset;
} update_section;

#ifdef CONFIG_TOUCHPAD_VIRTUAL_OFF
/*
 * Check if a block is within touchpad FW virtual address region, and
 * is therefore meant to be flashed to the touchpad.
 */
static int is_touchpad_block(uint32_t block_offset, size_t body_size)
{
	return (block_offset >= CONFIG_TOUCHPAD_VIRTUAL_OFF) &&
	       (block_offset + body_size) <= (CONFIG_TOUCHPAD_VIRTUAL_OFF +
					      CONFIG_TOUCHPAD_VIRTUAL_SIZE);
}
#endif

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
	if (update_section.base_offset != update_section.top_offset &&
	    (block_offset >= update_section.base_offset) &&
	    ((block_offset + body_size) <= update_section.top_offset)) {
		base = update_section.base_offset;
		size = update_section.top_offset - update_section.base_offset;
		/*
		 * If this is the first chunk for this section, it needs to
		 * be erased.
		 */
		if (block_offset == base) {
			if (crec_flash_physical_erase(base, size) !=
			    EC_SUCCESS) {
				CPRINTF("%s:%d erase failure of 0x%x..+0x%x\n",
					__func__, __LINE__, base, size);
				return UPDATE_ERASE_FAILURE;
			}
		}

		return UPDATE_SUCCESS;
	}

#ifdef CONFIG_TOUCHPAD_VIRTUAL_OFF
	if (is_touchpad_block(block_offset, body_size))
		return UPDATE_SUCCESS;
#endif

	CPRINTF("%s:%d %x, %d section base %x top %x\n", __func__, __LINE__,
		block_offset, body_size, update_section.base_offset,
		update_section.top_offset);

	return UPDATE_BAD_ADDR;
}

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

static int contents_allowed(uint32_t block_offset, size_t body_size,
			    void *update_data)
{
#if defined(CONFIG_TOUCHPAD_VIRTUAL_OFF) && defined(CONFIG_TOUCHPAD_HASH_FW)
	if (is_touchpad_block(block_offset, body_size)) {
		struct sha256_ctx ctx;
		uint8_t *tmp;
		uint32_t fw_offset = block_offset - CONFIG_TOUCHPAD_VIRTUAL_OFF;
		unsigned int chunk = fw_offset / CONFIG_UPDATE_PDU_SIZE;
		int good = 0;

		if (chunk >= CONFIG_TOUCHPAD_FW_CHUNKS ||
		    (fw_offset % CONFIG_UPDATE_PDU_SIZE) != 0) {
			CPRINTF("%s: TP invalid offset %08x\n", __func__,
				fw_offset);
			return 0;
		}

		SHA256_init(&ctx);
		SHA256_update(&ctx, update_data, body_size);
		tmp = SHA256_final(&ctx);

		good = !memcmp(tmp, touchpad_fw_hashes[chunk],
			       SHA256_DIGEST_SIZE);

		CPRINTF("%s: TP %08x %02x..%02x (%s)\n", __func__, fw_offset,
			tmp[0], tmp[31], good ? "GOOD" : "BAD");

		return good;
	}
#endif
	return 1;
}

/*
 * Setup internal state (e.g. valid sections, and fill first response).
 *
 * Assumes rpdu is already prefilled with 0, and that version has already
 * been set. May set a return_value != 0 on error.
 */
void fw_update_start(struct first_response_pdu *rpdu)
{
	const char *version;
#ifdef CONFIG_RWSIG_TYPE_RWSIG
	const struct vb21_packed_key *vb21_key;
#endif

	rpdu->header_type = htobe16(UPDATE_HEADER_TYPE_COMMON);

	/* Determine the valid update section. */
	switch (system_get_image_copy()) {
	case EC_IMAGE_RO:
		/* RO running, so update RW */
		update_section.base_offset = CONFIG_RW_MEM_OFF;
		update_section.top_offset = CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE;
		version = system_get_version(EC_IMAGE_RW);
		break;
	case EC_IMAGE_RW:
		/* RW running, so update RO */
		update_section.base_offset = CONFIG_RO_MEM_OFF;
		update_section.top_offset = CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE;
		version = system_get_version(EC_IMAGE_RO);
		break;
	default:
		CPRINTF("%s:%d\n", __func__, __LINE__);
		rpdu->return_value = htobe32(UPDATE_GEN_ERROR);
		return;
	}

	rpdu->common.maximum_pdu_size = htobe32(CONFIG_UPDATE_PDU_SIZE);
	rpdu->common.flash_protection = htobe32(crec_flash_get_protect());
	rpdu->common.offset = htobe32(update_section.base_offset);
	if (version)
		memcpy(rpdu->common.version, version,
		       sizeof(rpdu->common.version));

#ifdef CONFIG_ROLLBACK
	rpdu->common.min_rollback = htobe32(rollback_get_minimum_version());
#else
	rpdu->common.min_rollback = htobe32(-1);
#endif

#ifdef CONFIG_RWSIG_TYPE_RWSIG
	vb21_key = vb21_get_packed_key();
	rpdu->common.key_version = htobe32(vb21_key->key_version);
#endif

#ifdef HAS_TASK_RWSIG
	/* Do not allow the update to start if RWSIG is still running. */
	if (rwsig_get_status() == RWSIG_IN_PROGRESS) {
		CPRINTF("RWSIG in progress\n");
		rpdu->return_value = htobe32(UPDATE_RWSIG_BUSY);
	}
#endif
}

void fw_update_command_handler(void *body, size_t cmd_size,
			       size_t *response_size)
{
	struct update_command *cmd_body = body;
	void *update_data;
	uint8_t *error_code = body; /* Cache the address for code clarity. */
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
		/*
		 * TODO(b/36375666): The response size can be shorter depending
		 * on which board-specific type of response we provide. This
		 * may send trailing 0 bytes, which should be harmless.
		 */
		*response_size = sizeof(*rpdu);
		rpdu->protocol_version = htobe16(UPDATE_PROTOCOL_VERSION);

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

#ifdef CONFIG_TOUCHPAD_VIRTUAL_OFF
	if (is_touchpad_block(block_offset, body_size)) {
		if (touchpad_update_write(
			    block_offset - CONFIG_TOUCHPAD_VIRTUAL_OFF,
			    body_size, update_data) != EC_SUCCESS) {
			*error_code = UPDATE_WRITE_FAILURE;
			CPRINTF("%s:%d update write error\n", __func__,
				__LINE__);
			return;
		}

		new_chunk_written(block_offset);

		*error_code = UPDATE_SUCCESS;
		return;
	}
#endif

	CPRINTF("update: 0x%x\n", block_offset + CONFIG_PROGRAM_MEMORY_BASE);
	if (crec_flash_physical_write(block_offset, body_size, update_data) !=
	    EC_SUCCESS) {
		*error_code = UPDATE_WRITE_FAILURE;
		CPRINTF("%s:%d update write error\n", __func__, __LINE__);
		return;
	}

	new_chunk_written(block_offset);

	/* Verify that data was written properly. */
	if (memcmp(update_data,
		   (void *)(block_offset + CONFIG_PROGRAM_MEMORY_BASE),
		   body_size)) {
		*error_code = UPDATE_VERIFY_ERROR;
		CPRINTF("%s:%d update verification error\n", __func__,
			__LINE__);
		return;
	}

	*error_code = UPDATE_SUCCESS;
}

void fw_update_complete(void)
{
}
