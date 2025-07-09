# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for npcx9_monitor."""

register_raw_project(
    project_name="npcx_monitor",
    zephyr_board="npcx_evb/npcx9m6f",
    modules=["ec", "cmsis"],
    dts_overlays=["prj.dts"],
)
