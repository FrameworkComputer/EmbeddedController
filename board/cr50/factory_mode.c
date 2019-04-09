/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id.h"
#include "console.h"
#include "ccd_config.h"
#include "ec_commands.h"
#include "extension.h"
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

	if (board_id_is_blank(&id)) {
		CPRINTS("BID erased");
		return 1;
	}
	return 0;
}

static int inactive_image_is_guc_image(void)
{
	enum system_image_copy_t inactive_copy;
	const struct SignedHeader *other;

	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
		inactive_copy = SYSTEM_IMAGE_RW_B;
	else
		inactive_copy = SYSTEM_IMAGE_RW;
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
	return (!(system_get_reset_flags() & EC_RESET_FLAG_HIBERNATE) &&
		inactive_image_is_guc_image() && board_id_is_erased());
}

/*
 * Vendor command for ccd factory reset.
 *
 * This vendor command can be used to enable ccd and disable write protect with
 * a factory reset. A factory reset is automatically done during the first
 * factory boot, but this vendor command can be used to do a factory reset at
 * any time. Before calling factory reset, cr50 will make sure it is safe to do
 * so. Cr50 checks batt_is_present to make sure the user has physical access to
 * the device. Cr50 also checks ccd isn't disabled by the FWMP or ccd password.
 *
 * checks:
 * - batt_is_present - Factory reset can only be done if HW write protect is
 *              removed.
 * - FWMP disables ccd -  If FWMP has disabled ccd, then we can't bypass it with
 *              a factory reset.
 * - CCD password is set - If there is a password, someone will have to use that
 *              to open ccd and enable ccd manually. A factory reset cannot be
 *              used to get around the password.
 */
static enum vendor_cmd_rc vc_factory_reset(enum vendor_cmd_cc code,
					   void *buf,
					   size_t input_size,
					   size_t *response_size)
{
	*response_size = 0;

	if (input_size)
		return VENDOR_RC_BOGUS_ARGS;

	if (board_battery_is_present() || !board_fwmp_allows_unlock() ||
	    ccd_has_password())
		return VENDOR_RC_NOT_ALLOWED;

	CPRINTF("factory reset\n");
	enable_ccd_factory_mode(1);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_RESET_FACTORY, vc_factory_reset);
