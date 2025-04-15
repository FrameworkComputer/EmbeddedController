/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "body_detection.h"
#include "hooks.h"

namespace {
enum body_detect_states current_body_detect_state;
}

extern "C" enum body_detect_states body_detect_get_state() {
  return current_body_detect_state;
}

extern "C" void body_detect_change_state(enum body_detect_states state, bool) {
  current_body_detect_state = state;
  hook_notify(HOOK_BODY_DETECT_CHANGE);
}