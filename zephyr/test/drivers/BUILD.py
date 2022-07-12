# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Construct the drivers test binaries"""


drivers = register_host_test(
    test_name="drivers",
    dts_overlays=[
        here / "overlay.dts",
    ],
    kconfig_files=[
        here / "prj.conf",
    ],
)

isl923x = drivers.variant(
    project_name="test-drivers-isl923x",
)

usbc_alt_mode = drivers.variant(
    project_name="test-drivers-usbc_alt_mode",
)

led_driver = drivers.variant(
    project_name="test-drivers-led_driver",
    dts_overlays=[
        here / "led_driver" / "led_pins.dts",
        here / "led_driver" / "led_policy.dts",
    ],
    kconfig_files=[here / "led_driver" / "prj.conf"],
)
