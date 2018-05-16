/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id.h"
#include "console.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_CCD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CCD, format, ## args)


static int board_id_is_erased(void)
{
	struct board_id id;
	/*
	 * If we can't read the board id for some reason, return 0 just to be
	 * safe
	 */
	if (read_board_id(&id) != EC_SUCCESS) {
		CPRINTS("%s: BID read error", __func__);
		return 0;
	}

	/* If all of the fields are all 0xffffffff, the board id is not set */
	if (~(id.type & id.type_inv & id.flags) == 0) {
		CPRINTS("BID erased");
		return 1;
	}
	return 0;
}

static int inactive_image_is_guc_image(void)
{
	enum system_image_copy_t inactive_copy;
	const struct SignedHeader *other;

	if (system_get_image_copy() == SYSTEM_IMAGE_RW_A)
		inactive_copy = SYSTEM_IMAGE_RW_B;
	else
		inactive_copy = SYSTEM_IMAGE_RW_A;
	other = (struct SignedHeader *) get_program_memory_addr(
		inactive_copy);
	/*
	 * Chips from GUC are manufactured with 0.0.13 or 0.0.22. Compare the
	 * versions to determine if the inactive image is a GUC image.
	 */
	if (other->epoch_ == 0 && other->major_ == 0 &&
	    ((other->minor_ == 13) || (other->minor_ == 22))) {
		CPRINTS("GUC in inactive RW");
		return 1;
	}
	/*
	 * TODO(mruthven): Return true if factory image field of header is
	 * set
	 */
	return 0;
}

/**
 * Return non-zero if this is the first boot of a board in the factory.
 *
 * This is used to determine whether the default CCD configuration will be RMA
 * (things are unlocked for factory) or normal (things locked down because not
 * in factory).
 *
 * checks:
 * - If the system recovered from reboot not deep sleep resume.
 * - If the board ID exists, this is not the first boot
 * - If the inactive image is not a GUC image, then we've left the factory
 */
int board_is_first_factory_boot(void)
{
	return (!(system_get_reset_flags() & RESET_FLAG_HIBERNATE) &&
		inactive_image_is_guc_image() && board_id_is_erased());
}
