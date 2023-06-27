# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app
  PRIVATE
    src/test_ish_system.c
    ${PLATFORM_EC}/zephyr/shim/src/ish_system.c
)
