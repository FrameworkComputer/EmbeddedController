/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "Global.h"
#include "board.h"
#include "closed_source_set1.h"
#include "console.h"
#include "dcrypto.h"
#include "extension.h"
#include "hooks.h"
#include "nvmem.h"
#include "system.h"
#include "timer.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"

#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ## args)

static void disable_tpm(void)
{
	nvmem_enable_commits();
	tpm_stop();
	DCRYPTO_ladder_revoke();
	nvmem_clear_cache();

	if (board_uses_closed_source_set1())
		close_source_set1_disable_tpm();
}
DECLARE_DEFERRED(disable_tpm);

/*
 * On TPM reset event, tpm_reset_now() in tpm_registers.c clears TPM2 BSS memory
 * area. By placing s_tpm_mode in TPM2 BSS area, TPM mode value shall be
 * "TPM_MODE_ENABLED_TENTATIVE" on every TPM reset events.
 */
static enum tpm_modes s_tpm_mode __attribute__((section(".bss.Tpm2_common")));

static enum vendor_cmd_rc process_tpm_mode(struct vendor_cmd_params *p)
{
	uint8_t mode_val;
	uint8_t *buffer;

	p->out_size = 0;

	if (p->in_size > sizeof(uint8_t))
		return VENDOR_RC_NOT_ALLOWED;

	buffer = (uint8_t *)p->buffer;
	if (p->in_size == sizeof(uint8_t)) {

		if (!board_tpm_mode_change_allowed() ||
		    (s_tpm_mode != TPM_MODE_ENABLED_TENTATIVE))
			return VENDOR_RC_NOT_ALLOWED;

		mode_val = buffer[0];

		switch (mode_val) {
		case TPM_MODE_ENABLED:
			/*
			 * If Key ladder is disabled, then fail this request.
			 */
			if (!DCRYPTO_ladder_is_enabled())
				return VENDOR_RC_INTERNAL_ERROR;
			break;
		case TPM_MODE_DISABLED:
			/*
			 * If it is to be disabled, call disable_tpm() deferred
			 * so that this vendor command can be responded to
			 * before TPM stops.
			 */
			hook_call_deferred(&disable_tpm_data, 10 * MSEC);
			break;
		default:
			return VENDOR_RC_NO_SUCH_SUBCOMMAND;
		}
		s_tpm_mode = mode_val;
	} else {
		if (s_tpm_mode < TPM_MODE_DISABLED &&
		    !DCRYPTO_ladder_is_enabled())
			return VENDOR_RC_INTERNAL_ERROR;
	}

	p->out_size = sizeof(uint8_t);
	buffer[0] = (uint8_t) s_tpm_mode;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND_P(VENDOR_CC_TPM_MODE, process_tpm_mode);

enum tpm_modes get_tpm_mode(void)
{
	return s_tpm_mode;
}
