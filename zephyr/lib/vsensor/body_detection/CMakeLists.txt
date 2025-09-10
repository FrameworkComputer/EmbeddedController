# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)

pw_add_library(vsensor.body_detection STATIC
  SOURCES
    body_detection.c
    "${PLATFORM_EC}/common/body_detect_client.c"
    "${PLATFORM_EC}/common/body_detect_common.c"
  PRIVATE_DEPS
    cros_ec_interface
    math.smoothing
    math.iir_decimator
    math.iir_filter
)
