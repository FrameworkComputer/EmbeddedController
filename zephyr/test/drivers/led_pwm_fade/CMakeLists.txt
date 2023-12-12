# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add include paths
target_include_directories(app PRIVATE
    "${PLATFORM_EC}/zephyr/shim/src/led_driver")

# Add source files
target_sources(app PRIVATE
  src/led_pwm_fade.c
)
