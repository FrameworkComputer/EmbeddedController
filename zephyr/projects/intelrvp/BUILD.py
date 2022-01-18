# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# intelrvp has adlrvp_npcx, adlrvpp_ite, adlrvpp_mchp etc


def register_intelrvp_project(
    project_name,
    chip="npcx9",
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    register_func = register_binman_project
    if chip.startswith("npcx9"):
        register_func = register_npcx_project

    kconfig_files = [here / "prj.conf"]
    if project_name.startswith("adlrvp"):
        kconfig_files.append(here / "adlrvp/prj.conf")
    kconfig_files.extend(extra_kconfig_files)

    register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=extra_dts_overlays,
        kconfig_files=kconfig_files,
    )


register_intelrvp_project(
    project_name="adlrvp_npcx",
    chip="npcx9",
    extra_dts_overlays=[
        here / "adlrvp/adlrvp_npcx/adlrvp_npcx.dts",
        here / "adlrvp/adlrvp_npcx/gpio.dts",
        here / "adlrvp/adlrvp_npcx/interrupts.dts",
    ],
    extra_kconfig_files=[here / "adlrvp/adlrvp_npcx/prj.conf"],
)
