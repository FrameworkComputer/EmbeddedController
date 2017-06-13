/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"

#include "Global.h"
#include "board.h"
#include "console.h"
#include "endian.h"
#include "extension.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/*
 * The below structure represents the body of the response to the 'report tpm
 * state' vendor command.
 *
 * It will be transferred over the wire, so it needs to be
 * serialized/deserialized, and it is likely to change, so its contents must
 * be versioned.
 */
#define TPM_STATE_VERSION	1
struct tpm_state {
	uint32_t version;
	uint32_t fail_line;	/* s_failLIne */
	uint32_t fail_code;	/* s_failCode */
	char func_name[4];	/* s_failFunction, limited to 4 chars */
	uint32_t failed_tries;	/* gp.failedTries */
	uint32_t max_tries;	/* gp.maxTries */
	/* The below fields are present in version 2 and above. */
} __packed;

static void serialize_u32(void *buf, uint32_t value)
{
	value = htobe32(value);
	memcpy(buf, &value, sizeof(value));
}

static enum vendor_cmd_rc report_tpm_state(enum vendor_cmd_cc code,
					   void *buf,
					   size_t input_size,
					   size_t *response_size)
{
	struct tpm_state *state = buf;

	CPRINTS("%s", __func__);

	memset(state, 0, sizeof(*state));

	if (board_id_is_mismatched()) {
		s_failCode = 0xbadc0de;
		s_failLine = __LINE__;
		memcpy(&s_failFunction, __func__, sizeof(s_failFunction));
	}

	serialize_u32(&state->version, TPM_STATE_VERSION);
	serialize_u32(&state->fail_code, s_failCode);
	serialize_u32(&state->fail_line, s_failLine);
	serialize_u32(&state->failed_tries, gp.failedTries);
	serialize_u32(&state->max_tries, gp.maxTries);
	if (s_failFunction)
		memcpy(state->func_name, (void *)&s_failFunction,
		       sizeof(state->func_name));

	*response_size = sizeof(*state);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_REPORT_TPM_STATE, report_tpm_state);
