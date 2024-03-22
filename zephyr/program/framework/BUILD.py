# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def register_framework_project(
    project_name,
    chip="npcx9/npcx9m3f",
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

lotus = register_framework_project(
    project_name="lotus",
)

azalea = register_framework_project(
    project_name="azalea",
)

marigold = register_framework_project(
    project_name="marigold",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="lotus", addr=0X7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="azalea", addr=0X7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="marigold", addr=0X7FFE0)
