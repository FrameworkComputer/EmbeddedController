# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_binman_project(
    project_name="hayato",
    zephyr_board="asurada",
    dts_overlays=[
        "battery.dts",
        "gpio.dts",
        "interrupts.dts",
        "motionsense.dts",
        "pwm.dts",
    ],
)
