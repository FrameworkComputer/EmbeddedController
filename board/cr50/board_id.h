/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_BOARD_ID__H
#define __EC_BOARD_CR50_BOARD_ID__H

#include "util.h"

/* Structure holding Board ID */
struct board_id {
	uint32_t type;		/* Board type */
	uint32_t type_inv;	/* Board type (inverted) */
	uint32_t flags;		/* Flags */
};

/* Info1 Board space contents. */
struct info1_board_space {
	struct board_id bid;
};

#define INFO_BOARD_ID_SIZE		sizeof(struct board_id)
#define INFO_BOARD_SPACE_PROTECT_SIZE	16

BUILD_ASSERT((offsetof(struct info1_board_space, bid) & 3) == 0);
BUILD_ASSERT((INFO_BOARD_ID_SIZE & 3) == 0);
BUILD_ASSERT(sizeof(struct info1_board_space) <= INFO_BOARD_SPACE_PROTECT_SIZE);

#endif  /* ! __EC_BOARD_CR50_BOARD_ID_H */
