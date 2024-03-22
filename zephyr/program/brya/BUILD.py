# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for brya."""


def register_npcx9_variant(
    project_name, extra_dts_overlays=(), extra_kconfig_files=()
):
    """Register a variant of a brya, even though this is not named as such."""
    return register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m3f",
        dts_overlays=[
            "adc.dts",
            "battery.dts",
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
            here / "tcpc.conf",
            # Project-specific KConfig customization.
            *extra_kconfig_files,
        ],
        inherited_from=["brya"],
    )


brya = register_npcx9_variant(
    project_name="brya",
    extra_dts_overlays=[here / "brya.dts"],
    extra_kconfig_files=[here / "prj_brya.conf"],
)

brya_pdc = register_npcx_project(
    project_name="brya_pdc",
    zephyr_board="npcx9/npcx9m3f",
    dts_overlays=[here / "brya_pdc" / "project.overlay"],
    kconfig_files=[here / "prj.conf", here / "brya_pdc" / "project.conf"],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="brya", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="brya_pdc", addr=0x7FFE0)
