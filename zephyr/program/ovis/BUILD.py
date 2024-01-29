# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for Ovis."""


def register_ovis_project(
    project_name,
    kconfig_files=None,
):
    """Register a variant of Ovis."""
    if kconfig_files is None:
        kconfig_files = [
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ]

    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m3f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=kconfig_files,
        inherited_from=["ovis"],
    )


register_ovis_project(
    project_name="ovis",
)
register_ovis_project(
    project_name="deku",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="deku", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="ovis", addr=0x7FFE0)
