/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "cros/dsp/client.h"
#include "cros_board_info.h"

LOG_MODULE_DECLARE(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

/*
 * The functions in this file are thin wrappers used to bind the legacy API to
 * the new one in a way that's testable. Unit tests will directly call the
 * cbi_remote_{functionality} functions because the cbi_{functionality} will
 * act on the local data in the test. This avoids having to fork the test
 * process and using an IPC.
 */
/* LCOV_EXCL_START*/
int cbi_get_board_info(enum cbi_data_tag tag, uint8_t* buf, uint8_t* size) {
  LOG_DBG("remote_cbi.h::cbi_get_board_info()");
  return cbi_remote_get_board_info(tag, buf, size);
}

void cbi_invalidate_cache(void) {
  /* There's no need to do anything for invalidation because the client doesn't
   * cache the CBI values.
   */
}
/* LCOV_EXCL_STOP */
