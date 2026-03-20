# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Skywalker Makeifile
zephyr_include_directories("include")
zephyr_library_sources("src/sensor.c")
zephyr_library_sources("src/charger.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"src/keyboard_customization.c"
)
