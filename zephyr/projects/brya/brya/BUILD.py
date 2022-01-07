# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_npcx_project(
    project_name="brya",
    zephyr_board="brya",
    dts_overlays=[
        "cbi_eeprom.dts",
        "gpio.dts",
        "motionsense.dts",
        "pwm.dts",
    ],
)
