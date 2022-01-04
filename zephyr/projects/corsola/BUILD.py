# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Default chip is it8xxx2, some variants will use NPCX9X.


def register_corsola_project(
    project_name,
    chip="it8xxx2",
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    register_func = register_binman_project
    if chip.startswith("npcx9"):
        register_func = register_npcx_project

    register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[here / "power_signal.dts", *extra_dts_overlays],
        kconfig_files=[here / "prj.conf", *extra_kconfig_files],
    )


register_corsola_project(
    "krabby",
    extra_dts_overlays=[
        here / "adc_krabby.dts",
        here / "battery_krabby.dts",
        here / "gpio_krabby.dts",
        here / "i2c_krabby.dts",
        here / "motionsense_krabby.dts",
        here / "pwm_krabby.dts",
    ],
    extra_kconfig_files=[here / "prj_krabby.conf"],
)

register_corsola_project(
    project_name="kingler",
    chip="npcx9",
    extra_dts_overlays=[
        here / "adc_kingler.dts",
        here / "battery_kingler.dts",
        here / "gpio_kingler.dts",
        here / "i2c_kingler.dts",
    ],
    extra_kconfig_files=[here / "prj_kingler.conf"],
)
