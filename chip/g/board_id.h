/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_BOARD_ID_H
#define __EC_CHIP_G_BOARD_ID_H

#include "board_space.h"
#include "common.h"
#include "signed_header.h"
#include "util.h"

/**
 * Check the current header vs. the supplied Board ID
 *
 * @param board_id	Pointer to a Board ID structure to check
 * @param h		Pointer to the currently running image's header
 *
 * @return 0 if no mismatch, non-zero if mismatch
 */
uint32_t check_board_id_vs_header(const struct board_id *id,
				  const struct SignedHeader *h);

/**
 * Check board ID from the flash INFO1 space.
 *
 * @param id	Pointer to a Board ID structure to fill
 *
 * @return EC_SUCCESS of an error code in cases of vairous failures to read.
 */
int read_board_id(struct board_id *id);

/**
 * Return the image header for the current image copy
 */
const struct SignedHeader *get_current_image_header(void);

/**
 * Check if board ID in the image matches board ID field in the INFO1.
 *
 * Pass the pointer to the image header to check. If the pointer is set to
 * NULL, check board ID against the currently running image's header.
 *
 * Return true if there is a mismatch (the code should not run).
 */
uint32_t board_id_mismatch(const struct SignedHeader *h);

/**
 * Check if every field of the board id is 0xffffffff
 *
 * @param id	Pointer to a Board ID structure
 *
 * @return True if the board id is all 0xffffffff.
 */
int board_id_is_blank(const struct board_id *id);
#endif  /* ! __EC_CHIP_G_BOARD_ID_H */
