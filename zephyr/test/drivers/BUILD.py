# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Construct the drivers test binaries"""


default_config = {
    "dts_overlays": [
        here / "overlay.dts",
    ],
    "kconfig_files": [
        here / "prj.conf",
    ],
    "test_args": ["-flash={test_temp_dir}/flash.bin"],
}


def merge_dictionary(dict_1, dict_2):
    """Merge dict_1 and dict_2 and return the result"""
    dict_3 = {**dict_1, **dict_2}
    for key, value in dict_3.items():
        if key in dict_1 and key in dict_2:
            if isinstance(value, list) or isinstance(dict_1[key], list):
                dict_3[key] = value + dict_1[key]
            else:
                dict_3[key] = [value, dict_1[key]]
    return dict_3


register_host_test(
    "drivers",
    **default_config,
)

register_host_test(
    "drivers-isl923x",
    **default_config,
)

register_host_test(
    "drivers-led_driver",
    **merge_dictionary(
        default_config,
        {
            "dts_overlays": [
                here / "led_driver" / "led_pins.dts",
                here / "led_driver" / "led_policy.dts",
            ],
            "kconfig_files": [here / "led_driver" / "prj.conf"],
        },
    ),
)
