# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

brya = register_npcx_project(
    project_name="brya",
    zephyr_board="brya",
    dts_overlays=[
        "battery.dts",
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

ghost = brya.variant(project_name="ghost")
