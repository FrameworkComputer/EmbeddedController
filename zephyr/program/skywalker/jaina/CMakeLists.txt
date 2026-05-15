# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Jaina Makeifile
zephyr_include_directories("include")
zephyr_library_sources("src/sensor.c")
zephyr_library_sources("src/touchpanel.c" "src/pmic_resetb_irq.c")
zephyr_library_sources("src/keyboard_customization.c" "src/led.c")
