# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_npcx_project(
    "trogdor",
    zephyr_board="trogdor",
    dts_overlays=["gpio.dts", "battery.dts", "motionsense.dts"],
)
