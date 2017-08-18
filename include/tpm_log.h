/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TPM_LOG_H
#define __CROS_EC_TPM_LOG_H

#include "event_log.h"

enum tpm_event {
	TPM_EVENT_INIT,
	TPM_I2C_RESET,
};

/* Log TPM event of given type with data payload. */
void tpm_log_event(enum tpm_event type, uint16_t data);

#endif /* __CROS_EC_TPM_LOG_H */
