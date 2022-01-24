# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


def register_variant(project_name, extra_dts_overlays=(), extra_kconfig_files=()):
    register_npcx_project(
        project_name=project_name,
        zephyr_board="herobrine_npcx9",
        dts_overlays=[
            # Common to all projects.
            here / "battery.dts",
            here / "gpio.dts",
            here / "i2c.dts",
            here / "motionsense.dts",
            here / "switchcap.dts",
            here / "usbc.dts",
            # Project-specific DTS customization.
            *extra_dts_overlays,
        ],
        kconfig_files=[
            # Common to all projects.
            here / "prj.conf",
            # Project-specific KConfig customization.
            *extra_kconfig_files,
        ],
    )


register_variant(
    project_name="herobrine_npcx9",
    extra_kconfig_files=[here / "prj_herobrine_npcx9.conf"],
)
