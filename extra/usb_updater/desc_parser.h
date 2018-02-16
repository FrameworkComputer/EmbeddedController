/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EXTRA_USB_UPDATER_DESC_PARSER_H
#define __EXTRA_USB_UPDATER_DESC_PARSER_H

#include <stddef.h>
#include <stdint.h>

struct result_node {
	uint8_t expected_result[32];
};

enum range_type_t {
	NOT_A_RANGE,
	AP_RANGE,
	EC_RANGE,
	EC_GANG_RANGE,
};

struct addr_range {
	enum range_type_t range_type;
	uint32_t base_addr;
	uint32_t range_size;
	size_t variant_count; /* Set to zero for dump ranges. */
	struct result_node variants[0];
};

/* Board description retrieval API includes the following functions. */

/*
 * In the given hash database file find board by its ID. Return zero on
 * success, or OS error of error. In particular ENODATA is returned if the
 * section for the required board ID is not found in the file.
 */
int parser_find_board(const char *hash_file_name, const char board_id[4]);

/*
 * Find next range for the previousely defined board, parse it into the
 * addr_range structure and return pointer to the parsed structure to the
 * caller, set pointer to NULL if no more entries are available or in case of
 * error.
 *
 * Caller of this function is responsible for returning memory allocated for
 * the entry.
 *
 * Return value set to zero on success, or to OS error if one occurs. EIO is
 * used if an attmept to get next range is made before hash database file was
 * opened and board entry in it was found.
 */
int parser_get_next_range(struct addr_range **range);

/* Close the hash database file. */
void parser_done(void);

#endif // __EXTRA_USB_UPDATER_DESC_PARSER_H
