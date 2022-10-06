# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for npcx7_evb."""

register_npcx_project(
    project_name="npcx7",
    zephyr_board="npcx7_evb",
    dts_overlays=["gpio.dts", "interrupts.dts", "fan.dts", "keyboard.dts"],
)
