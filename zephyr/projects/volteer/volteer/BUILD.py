# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_npcx_project(
    project_name="volteer",
    zephyr_board="volteer",
    dts_overlays=[
        "bb_retimer.dts",
        "cbi_eeprom.dts",
        "fan.dts",
        "gpio.dts",
        "interrupts.dts",
        "keyboard.dts",
        "motionsense.dts",
        "pwm_leds.dts",
        "usbc.dts",
    ],
)
