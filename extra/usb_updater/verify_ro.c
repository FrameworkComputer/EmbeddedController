/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "desc_parser.h"
#include "gsctool.h"
#include "tpm_vendor_cmds.h"
#include "verify_ro.h"

/* Index of the matching hash variant. */
static ssize_t matching_variant;

/*
 * Print out passed in buffer contents in hex, 16 bytes per line, each line
 * starting with the base address value.
 *
 * If the passed in base address is not aligned at 16 byte boundary, skip
 * positions in the dump line so that the address is displayed rounded down to
 * the closest lower 16 byte boundary.
 *
 * For instance passing base of 0x4007 and size of 20 will result in a
 * printout like:
 *
 * 004000                      e0 00 00 00 00 66 c7 05 04
 * 004010 80 06 e0 06 00 66 c7 05 20 90 06
 *
 * If title is nonzero - print out the string it points to before printing
 * out buffer contents.
 */
static void print_buffer_aligned(const char *title, uint32_t base,
				 size_t size, const void *data)
{
	const uint8_t *bytes = data;
	size_t i;
	uint8_t alignment;

	/*
	 * Calculate how many characters we need to skip in the first dump
	 * line.
	 */
	alignment = base % 16;
	if (alignment) {
		size += alignment;
		base &= ~0xf;
	}

	if  (title)
		printf("%s\n", title);

	/* Let's print data space separated 16 bytes per line. */
	for (i = 0; i < size; i++) {
		if (!(i % 16))
			printf("\n%06zx", base + i);

		if (i < alignment)
			printf("   ");
		else
			printf(" %02x", bytes[i - alignment]);
	}
}

/* Change the DUT spihash range to the new_type value. */
static int set_new_range(struct transfer_descriptor *td,
			 enum range_type_t  new_type)
{
	uint32_t rv;
	struct vendor_cc_spi_hash_request req;

	memset(&req, 0, sizeof(req));

	/* Need to send command to change spihash mode. */
	switch (new_type) {
	case AP_RANGE:
		req.subcmd = SPI_HASH_SUBCMD_AP;
		break;
	case EC_RANGE:
		req.subcmd = SPI_HASH_SUBCMD_EC;
		break;
	case EC_GANG_RANGE:
		req.subcmd = SPI_HASH_SUBCMD_EC;
		req.flags = SPI_HASH_FLAG_EC_GANG;
		break;
	default: /* Should never happen. */
		return -EINVAL;
	}

	rv = send_vendor_command(td, VENDOR_CC_SPI_HASH, &req, sizeof(req),
				 0, NULL);

	if (!rv)
		return 0;

	if (rv == VENDOR_RC_IN_PROGRESS) {
		/* This will exit() on error. */
		poll_for_pp(td, VENDOR_CC_SPI_HASH, SPI_HASH_PP_POLL);
	} else {
		fprintf(stderr,
			"%s: failed setting range type %d, error %d\n",
			__func__, new_type, rv);
		return -EINVAL;
	}

	return 0;
}

/*
 * Verify a dump descriptor hash section defined by 'range'. The passed in by
 * pointer structure req has the range offset and size already initialized.
 *
 * Make sure that matching hashes are at the same index in the hash variants
 * arrays within the same board section.
 */
static int verify_hash_section(struct transfer_descriptor *td,
			       struct vendor_cc_spi_hash_request *req,
			       struct addr_range *range)
{
	size_t i;
	uint8_t response[sizeof(range->variants->expected_result)];
	size_t response_size;
	int rv;

	/* First retrieve hash from the DUT. */
	response_size = sizeof(response);
	req->subcmd = SPI_HASH_SUBCMD_SHA256;
	rv = send_vendor_command(td, VENDOR_CC_SPI_HASH,
				 req, sizeof(*req), response, &response_size);

	if (rv) {
		fprintf(stderr,
			"%s: failed retrieving hash at %x, tpm error %d\n",
			__func__, req->offset, rv);
		return -EINVAL;
	}

	if (response_size != sizeof(response)) {
		fprintf(stderr, "got %zd bytes in response for range %x:%x\n",
			response_size, req->offset, req->size);
		return -EINVAL;
	}

	if (matching_variant < 0) {
		/* This is the first hash range to be processed. */
		struct result_node *variant = range->variants;

		for (i = 0; i < range->variant_count; i++) {
			if (!memcmp(variant->expected_result,
				    response, response_size)) {
				matching_variant = i;
				return 0;
			}
			variant++;
		}

		fprintf(stderr, "no matching hash found for range %x:%x\n",
			req->offset, req->size);
		return -EINVAL;
	}

	if (!memcmp(range->variants[matching_variant].expected_result,
		    response, response_size))
		return 0;

	fprintf(stderr, "hash mismatch for range %x:%x\n",
		req->offset, req->size);

	return -EINVAL;
}

/*
 * Dump DUT's memory in the range defined by contents of the passed in req
 * structure.
 *
 * The Cr50 SPI hash dump vendor command implementation limits size of the
 * dump to 32, so in case the caller requests more than 32 bytes retrieve them
 * in 32 byte blocks.
 *
 * If base address of the range is not aligned at 16, retrieve smaller
 * quantity such that the following transactions retrieve block starting at
 * aligned addresses, this makes for a better looking hex dump.
 */
static int dump_range(struct transfer_descriptor *td,
		      struct vendor_cc_spi_hash_request *req)
{
	size_t  remaining_size = req->size;
	size_t response_size;
	/* Max size of a single shot is 32 bytes. */
	const size_t max_transfer = 32;
	uint8_t response[max_transfer];

	req->subcmd = SPI_HASH_SUBCMD_DUMP;
	while (remaining_size) {
		size_t shot_size = max_transfer;
		uint8_t alignment;
		uint32_t rv;

		alignment = req->offset % 16;

		if (alignment && ((alignment + remaining_size) > max_transfer))
			/* first line should be truncated. */
			shot_size = max_transfer - alignment;
		else if (shot_size > remaining_size)
			shot_size = remaining_size;

		req->size = shot_size;
		response_size = shot_size;
		rv = send_vendor_command(td, VENDOR_CC_SPI_HASH,
					 req, sizeof(*req), response,
					 &response_size);
		if (rv) {
			fprintf(stderr,
				"%s: failed getting dump contents at %x\n",
				__func__, req->offset);
			return -EINVAL;
		}

		if (response_size != shot_size) {
			fprintf(stderr,
				"%s: dump error: got %zd bytes, expected %zd\n",
				__func__, response_size, shot_size);
			return -EINVAL;
		}

		print_buffer_aligned(NULL, req->offset, shot_size, response);
		remaining_size -= shot_size;
		req->offset += shot_size;
	}
	printf("\n");

	return 0;
}

/*
 * Iterate through sections of a board descriptor database, retrieving hashes
 * or straight memory blocks as defined by description sections.
 */
static int process_descriptor_sections(struct transfer_descriptor *td)
{
	struct vendor_cc_spi_hash_request req;
	int rv;
	struct addr_range *range;
	enum range_type_t current_range = NOT_A_RANGE;

	do {
		/*
		 * Retrieve next range descriptor section from the descriptor
		 * database. The function below is guaranteed to set range to
		 * NULL on any error.
		 */
		rv = parser_get_next_range(&range);
		if (rv) {
			 /*
			  * ENODATA means all board's sections have been
			  * processed.
			  */
			if (rv == -ENODATA)
				rv = 0;
			break;
		}

		if (current_range != range->range_type) {
			rv = set_new_range(td, range->range_type);
			if (rv)
				break;
		}

		memset(&req, 0, sizeof(req));
		req.offset = range->base_addr;
		req.size = range->range_size;

		if (range->variant_count)
			rv = verify_hash_section(td, &req, range);
		else
			rv = dump_range(td, &req);

		free(range);
		range = NULL;
	}  while (!rv);

	if (range)
		free(range);

	return rv;
}

int verify_ro(struct transfer_descriptor *td,
	      const char *desc_file_name,
	      bool show_machine_output)
{
	/* First find out board ID of the target. */
	struct board_id bid;
	char rlz_code[sizeof(bid.type) + 1];
	int section_count = 0;
	int rv = 0;

	/*
	 * Find out what Board ID is the device we are talking to. This
	 * function calls exit() on any error.
	 */
	process_bid(td, bid_get, &bid, show_machine_output);

	if (bid.type != ~bid.type_inv) {
		fprintf(stderr, "Inconsistent board ID: %08x != ~%08x\n",
			bid.type, bid.type_inv);
		return -EINVAL;
	}

	/*
	 * Convert bid from int to asciiz so that it could be used for
	 * strcmp() on the descriptor file section headers.
	 */
	memcpy(rlz_code, &bid.type, sizeof(rlz_code) - 1);
	rlz_code[sizeof(rlz_code) - 1] = '\0';

	while (!parser_find_board(desc_file_name, rlz_code)) {

		/*
		 * Each board section might have different index of the
		 * matching hash variant.
		 */
		matching_variant = -1;

		section_count++;
		rv = process_descriptor_sections(td);
		if (rv)
			break;
	}

	if (section_count != 2) {
		printf("Found wrong number of sections (%d) for board ID %s\n",
		       section_count, rlz_code);
		rv = -EINVAL;
	} else if (!rv) {
		/*
		 * Check was successful, send command to exit verification
		 * mode.
		 */
		struct vendor_cc_spi_hash_request req;

		memset(&req, 0, sizeof(req));
		req.subcmd = SPI_HASH_SUBCMD_DISABLE;
		rv = send_vendor_command(td, VENDOR_CC_SPI_HASH, &req,
					 sizeof(req), 0, NULL);
		if (rv) {
			fprintf(stderr,
				"%s: spi hash disable TPM error %d\n",
				__func__, rv);
			rv = -EINVAL;
		}
	}

	parser_done();
	return rv;
}
