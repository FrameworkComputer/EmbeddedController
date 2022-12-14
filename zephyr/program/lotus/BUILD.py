# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_npcx_project(
    project_name="lotus",
    zephyr_board="npcx9m3f",
    dts_overlays=[
        here / "gpio.dts",
        here / "led_pins.dts",
        here / "lotus.dts",
    ],
)
