# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources("src/extpower_custom.c")
zephyr_library_sources("src/chipset.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CUSTOM_FAN_DUTY_CONTROL "src/fan.c")
