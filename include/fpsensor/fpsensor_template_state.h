/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_TEMPLATE_STATE_H
#define __CROS_EC_FPSENSOR_TEMPLATE_STATE_H

#include "ec_commands.h"

#include <stdbool.h>

#include <array>
#include <variant>

extern "C" {
#include "fpsensor_driver.h"
}

/* The extra information for the encrypted template.
 * Note: the encrypted template content and encrypted positive match salt will
 * be stored in the fp_template[] and fp_positive_match_salt[] */
struct fp_encrypted_template_state {
	ec_fp_template_encryption_metadata enc_metadata;
};

/* The extra information for the decrypted template. */
struct fp_decrypted_template_state {
	/* The user_id that will be used to check the unlock template request is
	 * valid or not. */
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id;
};

/* The template can only be one of these states.
 * Note: std::monostate means there is nothing in this template. */
using fp_template_state =
	std::variant<std::monostate, fp_encrypted_template_state,
		     fp_decrypted_template_state>;

#endif /* __CROS_EC_FPSENSOR_TEMPLATE_STATE_H */
