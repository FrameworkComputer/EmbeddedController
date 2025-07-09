# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories(include)

zephyr_library_sources("src/touch_panel.c")
zephyr_library_sources("src/common.c")
zephyr_library_sources("src/sensor.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"src/keyboard_customization.c"
)
zephyr_library_sources("src/mp2964_update.c")
zephyr_library_sources("src/thermal.c")
