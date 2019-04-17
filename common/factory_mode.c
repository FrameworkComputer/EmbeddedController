/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* CCD factory enable */

#include "ccd_config.h"
#include "console.h"
#include "extension.h"
#include "hooks.h"
#include "system.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"

#define CPRINTS(format, args...) cprints(CC_CCD, format, ## args)

static uint8_t wait_for_factory_ccd_change;
static uint8_t reset_required_;

static void factory_config_saved(int saved)
{
	wait_for_factory_ccd_change = 0;

	CPRINTS("%s: %s%s", __func__, saved ? "done" : "failed",
		reset_required_ ? ", rebooting" : "");

	if (!reset_required_)
		return;

	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

static void ccd_config_changed(void)
{
	if (!wait_for_factory_ccd_change)
		return;

	factory_config_saved(1);
}
DECLARE_HOOK(HOOK_CCD_CHANGE, ccd_config_changed, HOOK_PRIO_LAST);

static void force_system_reset(void)
{
	CPRINTS("%s: ccd hook didn't reset the system");
	factory_config_saved(0);
}
DECLARE_DEFERRED(force_system_reset);

/* The below time constants are way longer than should be required in practice:
 *
 * Time it takes to finish processing TPM command
 */
#define TPM_PROCESSING_TIME (1 * SECOND)

/*
 * Time it takse TPM reset function to wipe out the NVMEM and reboot the
 * device.
 */
#define TPM_RESET_TIME (10 * SECOND)

/* Total time deep sleep should not be allowed. */
#define DISABLE_SLEEP_TIME (TPM_PROCESSING_TIME + TPM_RESET_TIME)

static void factory_enable_deferred(void)
{
	int rv;

	if (board_wipe_tpm(reset_required_) != EC_SUCCESS)
		return;

	CPRINTS("%s: TPM reset done, enabling factory mode", __func__);

	wait_for_factory_ccd_change = 1;
	rv = ccd_reset_config(CCD_RESET_FACTORY);
	if (rv != EC_SUCCESS)
		factory_config_saved(0);

	if (reset_required_) {
		/*
		 * Cr50 will reset once factory mode is enabled. If it hasn't in
		 * TPM_RESET_TIME, declare factory enable failed and force the
		 * reset.
		 */
		hook_call_deferred(&force_system_reset_data, TPM_RESET_TIME);
	}
}
DECLARE_DEFERRED(factory_enable_deferred);

void enable_ccd_factory_mode(int reset_required)
{
	delay_sleep_by(DISABLE_SLEEP_TIME);

	reset_required_ |= !!reset_required;
	hook_call_deferred(&factory_enable_deferred_data,
		TPM_PROCESSING_TIME);
}
