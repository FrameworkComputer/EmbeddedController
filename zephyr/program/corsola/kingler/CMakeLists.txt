# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Kingler Makeifile

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C "../src/npcx_i2c.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
                             "../src/npcx_usb_pd_policy.c" "../src/npcx_usbc.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BUTTONS_RUNTIME_CONFIG
                             "src/button.c")
