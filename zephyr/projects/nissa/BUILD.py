# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Nivviks has NPCX993F, Nereid has ITE81302


def register_nissa_project(
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
        dts_overlays=[*extra_dts_overlays],
        kconfig_files=[here / "prj.conf", *extra_kconfig_files],
    )


register_nissa_project(
    project_name="nivviks",
    chip="npcx9",
    extra_dts_overlays=[
        here / "nivviks_generated.dts",
        here / "nivviks_overlay.dts",
    ],
    extra_kconfig_files=[here / "prj_nivviks.conf"],
)

register_nissa_project(
    project_name="nereid",
    chip="it8xxx2",
    extra_dts_overlays=[
        here / "nereid_generated.dts",
        here / "nereid_overlay.dts",
    ],
    extra_kconfig_files=[here / "prj_nereid.conf"],
)
