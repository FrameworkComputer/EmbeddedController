# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_EC_KEYBOARD_INPUT keyboard_input.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC_COL_GPIO_DRIVE col_gpio_drive.c)
