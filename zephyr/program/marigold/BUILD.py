# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def register_lotus_project(
    project_name,
    chip="npcx9m3f",
):
    """Register a variant of lotus."""
    register_func = register_npcx_project

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
    )

lotus = register_lotus_project(
    project_name="lotus",
    chip="npcx9m3f",
)

azalea = register_lotus_project(
    project_name="azalea",
    chip="npcx9m3f",
)
