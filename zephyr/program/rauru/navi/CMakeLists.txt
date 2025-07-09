# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

zephyr_library_sources("src/led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/dp_alt_mode.c")
zephyr_library_sources("src/usb_mux.c")
zephyr_library_sources("src/touch.c")
zephyr_library_sources("src/suspend_resume.c")
zephyr_library_sources("src/thermal.c")
zephyr_library_sources("src/power_config.c")
