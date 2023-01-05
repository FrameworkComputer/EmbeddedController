# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_GPIO_KEYS_ZEPHYR        buttons.c)
zephyr_library_sources_ifdef(CONFIG_GPIO_KEYS_ZEPHYR        power_button.c)
