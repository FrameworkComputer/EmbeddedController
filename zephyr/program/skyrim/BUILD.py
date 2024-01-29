# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for skyrim."""


def register_skyrim_project(
    project_name,
):
    """Register a variant of skyrim."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m3f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
        inherited_from=["skyrim"],
    )


register_skyrim_project(
    project_name="skyrim",
)


register_skyrim_project(
    project_name="winterhold",
)


register_skyrim_project(
    project_name="frostflow",
)

register_skyrim_project(
    project_name="crystaldrift",
)

register_skyrim_project(
    project_name="markarth",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="crystaldrift", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="frostflow", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="markarth", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="skyrim", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="winterhold", addr=0x7FFE0)
