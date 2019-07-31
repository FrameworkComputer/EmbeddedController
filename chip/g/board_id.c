/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "board_id.h"
#include "endian.h"
#include "extension.h"
#include "flash_info.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/**
 * Return the image header for the current image copy
 */
const struct SignedHeader *get_current_image_header(void)
{
	return (const struct SignedHeader *)
			get_program_memory_addr(system_get_image_copy());
}

int board_id_is_blank(const struct board_id *id)
{
	return ~(id->type & id->type_inv & id->flags) == 0;
}

uint32_t check_board_id_vs_header(const struct board_id *id,
				  const struct SignedHeader *h)
{
	uint32_t mismatch;
	uint32_t header_board_id_type;
	uint32_t header_board_id_mask;
	uint32_t header_board_id_flags;

	/* Blank Board ID matches all headers */
	if (board_id_is_blank(id))
		return 0;

	header_board_id_type = SIGNED_HEADER_PADDING ^ h->board_id_type;
	header_board_id_mask = SIGNED_HEADER_PADDING ^ h->board_id_type_mask;
	header_board_id_flags = SIGNED_HEADER_PADDING ^ h->board_id_flags;

	/*
	 * Masked bits in header Board ID type must match type and inverse from
	 * flash.
	 */
	mismatch = header_board_id_type ^ id->type;
	mismatch |= header_board_id_type ^ ~id->type_inv;
	mismatch &= header_board_id_mask;

	/*
	 * All 1-bits in header Board ID flags must be present in flags from
	 * flash
	 */
	mismatch |=
		((header_board_id_flags & id->flags) != header_board_id_flags);

	return mismatch;
}

int read_board_id(struct board_id *id)
{
	uint32_t *id_p;
	int i;

	/*
	 * Board ID structure size is guaranteed to be divisible by 4, and it
	 * is guaranteed to be aligned at 4 bytes.
	 */

	id_p = (uint32_t *)id;

	for (i = 0; i < sizeof(*id); i += sizeof(uint32_t)) {
		int rv;

		rv = flash_physical_info_read_word
			(INFO_BOARD_ID_OFFSET + i, id_p);
		if (rv != EC_SUCCESS) {
			CPRINTF("%s: failed to read word %d, error %d\n",
				__func__, i, rv);
			return rv;
		}
		id_p++;
	}
	return EC_SUCCESS;
}

uint32_t board_id_mismatch(const struct SignedHeader *sh)
{
	struct board_id id;

	if (!sh)
		/* Get header of the currently running image. */
		sh = get_current_image_header();

	/* Get Board ID from INFO1. */
	if (read_board_id(&id) != EC_SUCCESS) {
		/*
		 * On failure, set id fields to 0.  This will only match an
		 * unrestricted image header (board_id_mask=board_id_flags=0),
		 * which would run on any Board ID.
		 *
		 * Don't return error, because that would prevent all images
		 * from running.
		 */
		id.type = id.type_inv = id.flags = 0;
	}

	return check_board_id_vs_header(&id, sh);
}

/**
 * Write board ID into the flash INFO1 space.
 *
 * @param id	Pointer to a Board ID structure to copy into INFO1
 *
 * @return EC_SUCCESS or an error code in cases of various failures to read or
 *              if the space has been already initialized.
 */
static int write_board_id(const struct board_id *id, int clear_flags)
{
	struct board_id id_test;
	uint32_t rv;

	/*
	 * Make sure the current header will still validate against the
	 * proposed values.  If it doesn't, then programming these values
	 * would cause the next boot to fail.
	 */
	if (check_board_id_vs_header(id, get_current_image_header()) != 0) {
		CPRINTS("%s: Board ID wouldn't allow current header", __func__);
		return EC_ERROR_INVAL;
	}

	/* Fail if Board ID is already programmed */
	rv = read_board_id(&id_test);
	if (rv != EC_SUCCESS) {
		CPRINTS("%s: error reading Board ID", __func__);
		return rv;
	}

	if (!clear_flags && !board_id_is_blank(&id_test)) {
		CPRINTS("%s: Board ID already programmed", __func__);
		return EC_ERROR_ACCESS_DENIED;
	}

	flash_info_write_enable();

	/* Write Board ID */
	rv = flash_info_physical_write(INFO_BOARD_ID_OFFSET +
			 offsetof(struct info1_board_space, bid),
				       sizeof(*id), (const char *)id);
	if (rv != EC_SUCCESS)
		CPRINTS("%s: write failed", __func__);

	flash_info_write_disable();

	return rv;
}

static enum vendor_cmd_rc vc_set_board_id(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	struct board_id id;
	uint8_t *pbuf = buf;

	*response_size = 1;

	/* Exactly two fields are expected. */
	if (input_size != sizeof(id.type) + sizeof(id.flags)) {
		*pbuf = VENDOR_RC_BOGUS_ARGS;
		return VENDOR_RC_BOGUS_ARGS;
	}

	memcpy(&id.type, pbuf, sizeof(id.type));
	id.type = be32toh(id.type);
	id.type_inv = ~id.type;

	memcpy(&id.flags, pbuf + sizeof(id.type), sizeof(id.flags));
	id.flags = be32toh(id.flags);

	/* We care about the LSB only. */
	*pbuf = write_board_id(&id, 0);

	return *pbuf;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SET_BOARD_ID, vc_set_board_id);

static int command_board_id(int argc, char **argv)
{
	struct board_id id;
	int rv = EC_ERROR_PARAM_COUNT;

	if (argc == 1) {
		rv = read_board_id(&id);

		if (rv != EC_SUCCESS) {
			ccprintf("Failed to read board ID space\n");
			return rv;
		}
		ccprintf("Board ID: %08x, flags %08x\n", id.type, id.flags);

		if (board_id_is_blank(&id))
			return rv; /* The space is not initialized. */

		if (id.type != ~id.type_inv)
			ccprintf("Inv Type Mismatch (%08x instead of %08x)!\n",
				 id.type_inv, ~id.type);
	}
#ifdef CR50_DEV
	else if (argc == 3) {
		char *e;

		id.type = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		id.type_inv = ~id.type;
		id.flags = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		rv = write_board_id(&id, 0);
	}
#endif
	return rv;
}
DECLARE_SAFE_CONSOLE_COMMAND(bid, command_board_id,
#ifdef CR50_DEV
			     "[bid flags]",
#else
			     NULL,
#endif
			     "Set/Get Board ID");

static enum vendor_cmd_rc vc_get_board_id(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	struct board_id id;

	if (read_board_id(&id))
		return VENDOR_RC_READ_FLASH_FAIL;

	/* Convert to line representation. */
	id.type = htobe32(id.type);
	id.type_inv = htobe32(id.type_inv);
	id.flags = htobe32(id.flags);

	memcpy(buf, &id, sizeof(id));
	*response_size = sizeof(id);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_BOARD_ID, vc_get_board_id);
