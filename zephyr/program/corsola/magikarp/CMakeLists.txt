# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Magikarp Makeifile

zephyr_library_sources("../src/ite_hooks.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C "../src/ite_i2c.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
                             "../src/ite_usb_pd_policy.c" "../src/ite_usbc.c")

zephyr_library_sources("src/sensor.c" "src/ppc.c")
