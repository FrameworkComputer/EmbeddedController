# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for skyrim."""


def register_skyrim_project(
    project_name,
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    """Register a variant of skyrim."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m3f",
        dts_overlays=[
            # Common to all projects.
            here / "adc.dts",
            here / "fan.dts",
            here / "gpio.dts",
            here / "interrupts.dts",
            here / "keyboard.dts",
            here / "motionsense.dts",
            here / "usbc.dts",
            # Project-specific DTS customizations.
            *extra_dts_overlays,
        ],
        kconfig_files=[here / "prj.conf", *extra_kconfig_files],
    )


register_skyrim_project(
    project_name="morthal",
    extra_dts_overlays=[
        here / "morthal.dts",
        here / "battery_morthal.dts",
        here / "led_pins_morthal.dts",
        here / "led_policy_morthal.dts",
    ],
    extra_kconfig_files=[
        here / "prj_morthal.conf",
    ],
)


register_skyrim_project(
    project_name="skyrim",
    extra_dts_overlays=[
        here / "skyrim.dts",
        here / "battery_skyrim.dts",
        here / "led_pins_skyrim.dts",
        here / "led_policy_skyrim.dts",
    ],
    extra_kconfig_files=[
        here / "prj_skyrim.conf",
    ],
)


register_skyrim_project(
    project_name="winterhold",
    extra_dts_overlays=[
        here / "winterhold.dts",
        here / "battery_winterhold.dts",
        here / "led_pins_winterhold.dts",
        here / "led_policy_winterhold.dts",
    ],
    extra_kconfig_files=[
        here / "prj_winterhold.conf",
    ],
)


register_skyrim_project(
    project_name="frostflow",
    extra_dts_overlays=[
        here / "frostflow.dts",
        here / "battery_frostflow.dts",
        here / "led_pins_frostflow.dts",
        here / "led_policy_frostflow.dts",
    ],
    extra_kconfig_files=[
        here / "prj_frostflow.conf",
    ],
)
