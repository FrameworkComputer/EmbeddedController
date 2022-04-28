# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for brya."""


def register_npcx9_variant(project_name, extra_dts_overlays=(), extra_kconfig_files=()):
    """Register a variant of a brya, even though this is not named as such."""
    return register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m3f",
        dts_overlays=[
            "adc.dts",
            "battery.dts",
            "bb_retimer.dts",
            "cbi_eeprom.dts",
            "fan.dts",
            "gpio.dts",
            "i2c.dts",
            "interrupts.dts",
            "keyboard.dts",
            "motionsense.dts",
            "pwm_leds.dts",
            "temp_sensors.dts",
            "usbc.dts",
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


brya = register_npcx9_variant(
    project_name="brya",
    extra_dts_overlays=[here / "brya.dts"],
    extra_kconfig_files=[here / "prj_brya.conf"],
)

ghost = brya.variant(
    project_name="ghost",
    kconfig_files=[here / "prj_ghost.conf"],
)
