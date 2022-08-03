# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Rex Projects."""


def register_variant(
    project_name, extra_dts_overlays=(), extra_kconfig_files=()
):
    """Register a variant of rex."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m7f",
        dts_overlays=[
            # Common to all projects.
            here / "rex.dts",
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
    project_name="rex",
    extra_dts_overlays=[
        here / "generated.dts",
        here / "interrupts.dts",
        here / "power_signals.dts",
        here / "battery.dts",
        here / "usbc.dts",
    ],
    extra_kconfig_files=[here / "prj_rex.conf"],
)
