# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_binman_project(
    "krabby",
    zephyr_board="it8xxx2",
    dts_overlays=["battery.dts", "gpio.dts", "motionsense.dts", "pwm.dts"],
)
