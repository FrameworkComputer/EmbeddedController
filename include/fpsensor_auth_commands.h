/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_AUTH_COMMANDS_H
#define __CROS_EC_FPSENSOR_AUTH_COMMANDS_H

extern "C" {
#include "common.h"
}

/**
 * Check the context has been cleared or not.
 *
 * @return EC_SUCCESS if the context has been cleared
 * @return EC_ERROR_ACCESS_DENIED on the other cases.
 */
enum ec_error_list check_context_cleared();

#endif /* __CROS_EC_FPSENSOR_AUTH_COMMANDS_H */
