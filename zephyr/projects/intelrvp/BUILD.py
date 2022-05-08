# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for intelrvp."""

# intelrvp has adlrvp_npcx, adlrvpp_ite, adlrvpp_mchp etc


def register_intelrvp_project(
    project_name,
    chip="npcx9",
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    """Register a variant of intelrvp."""
    register_func = register_binman_project
    if chip.startswith("npcx9"):
        register_func = register_npcx_project

    kconfig_files = [here / "prj.conf"]
    dts_overlays = []
    if project_name.startswith("adlrvp"):
        kconfig_files.append(here / "adlrvp/prj.conf")
        dts_overlays.append(here / "adlrvp/battery.dts")
        dts_overlays.append(here / "adlrvp/ioex.dts")
    if project_name.startswith("mtlrvp"):
        kconfig_files.append(here / "mtlrvp/prj.conf")
        dts_overlays.append(here / "mtlrvp/battery.dts")
    kconfig_files.extend(extra_kconfig_files)
    dts_overlays.extend(extra_dts_overlays)

    register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=dts_overlays,
        kconfig_files=kconfig_files,
    )


register_intelrvp_project(
    project_name="adlrvp_npcx",
    chip="npcx9",
    extra_dts_overlays=[
        here / "adlrvp/adlrvp_npcx/adlrvp_npcx.dts",
        here / "adlrvp/adlrvp_npcx/bb_retimer.dts",
        here / "adlrvp/adlrvp_npcx/cbi_eeprom.dts",
        here / "adlrvp/adlrvp_npcx/fan.dts",
        here / "adlrvp/adlrvp_npcx/gpio.dts",
        here / "adlrvp/adlrvp_npcx/interrupts.dts",
        here / "adlrvp/adlrvp_npcx/keyboard.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
        here / "adlrvp/adlrvp_npcx/usbc.dts",
    ],
    extra_kconfig_files=[
        here / "legacy_ec_pwrseq.conf",
        here / "adlrvp/adlrvp_npcx/prj.conf",
    ],
)

register_intelrvp_project(
    project_name="mtlrvpp_npcx",
    chip="npcx9",
    extra_dts_overlays=[
        here / "adlrvp/adlrvp_npcx/cbi_eeprom.dts",
        here / "mtlrvp/mtlrvpp_npcx/fan.dts",
        here / "mtlrvp/mtlrvpp_npcx/gpio.dts",
        here / "mtlrvp/mtlrvpp_npcx/interrupts.dts",
        here / "mtlrvp/ioex.dts",
        here / "mtlrvp/mtlrvpp_npcx/mtlrvp_npcx.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
    ],
    extra_kconfig_files=[
        here / "legacy_ec_pwrseq.conf",
        here / "mtlrvp/mtlrvpp_npcx/prj.conf",
    ],
)
