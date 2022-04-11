# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for skyrim."""


def register_variant(project_name):
    """Register a variant of skyrim."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9",
        dts_overlays=[
            # Common to all projects.
            here / "adc.dts",
            here / "battery.dts",
            here / "fan.dts",
            here / "gpio.dts",
            here / "i2c.dts",
            here / "interrupts.dts",
            here / "keyboard.dts",
            here / "motionsense.dts",
            here / "pwm_leds.dts",
            here / "usbc.dts",
            # Project-specific DTS customizations.
            here / f"{project_name}.dts",
        ],
        kconfig_files=[
            here / f"prj_{project_name}.conf",
        ],
    )


register_variant(project_name="skyrim")

# TODO: Deprecate guybrush build after skyrim hardware is readily available.
register_variant(project_name="guybrush")
