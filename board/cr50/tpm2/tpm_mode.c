/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "Global.h"
#include "console.h"
#include "extension.h"
#include "hooks.h"
#include "timer.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"

#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ## args)

DECLARE_DEFERRED(tpm_stop);

/*
 * On TPM reset event, tpm_reset_now() in tpm_registers.c clears TPM2 BSS memory
 * area. By placing s_tpm_mode in TPM2 BSS area, TPM mode value shall be
 * "TPM_MODE_ENABLED_TENTATIVE" on every TPM reset events.
 */
static enum tpm_modes s_tpm_mode __attribute__((section(".bss.Tpm2_common")));

static enum vendor_cmd_rc set_tpm_mode(struct vendor_cmd_params *p)
{
	uint8_t mode_val;
	uint8_t *buffer;

	p->out_size = 0;

	if (p->in_size > sizeof(uint8_t))
		return VENDOR_RC_NOT_ALLOWED;

	buffer = (uint8_t *)p->buffer;
	if (p->in_size == sizeof(uint8_t)) {
		if (s_tpm_mode != TPM_MODE_ENABLED_TENTATIVE)
			return VENDOR_RC_NOT_ALLOWED;
		mode_val = buffer[0];
		if (mode_val == TPM_MODE_DISABLED)
			hook_call_deferred(&tpm_stop_data, 10 * MSEC);
		else if (mode_val != TPM_MODE_ENABLED)
			return VENDOR_RC_NOT_ALLOWED;
		s_tpm_mode = mode_val;
	}

	p->out_size = sizeof(uint8_t);
	buffer[0] = (uint8_t) s_tpm_mode;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND_P(VENDOR_CC_TPM_MODE, set_tpm_mode);

enum tpm_modes get_tpm_mode(void)
{
	return s_tpm_mode;
}

