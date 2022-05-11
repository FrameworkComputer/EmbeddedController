# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC           system.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_ITE   keyboard_raw.c)
zephyr_library_sources_ifdef(CONFIG_PM_POLICY_CUSTOM  power_policy.c)
