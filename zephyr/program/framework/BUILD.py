# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def register_framework_project(
    project_name,
    chip="npcx9/npcx9m3f",
    # set the default to laptop
    product_family="laptop",
):
    """Register a variant of framework."""
    register_func = register_npcx_project

    if product_family != "desktop":
        kconfig_files = [
            # Common to all laptop projects
            here / "program.conf",
            # Project-specific KConfig customization
            here / project_name / "project.conf",
        ]
    else:
        kconfig_files = [
            # Common to all laptop projects
            here / "desktop_program.conf",
            # Project-specific KConfig customization
            here / project_name / "project.conf",
        ]

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=kconfig_files,
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
assert_rw_fwid_DO_NOT_EDIT(project_name="lotus", addr=0X7EFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="azalea", addr=0X7EFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="marigold", addr=0X7FFE0)
