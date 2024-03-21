# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for myst."""


def register_myst_project(
    project_name,
):
    """Register a variant of myst."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
        inherited_from=["myst"],
    )


register_myst_project(
    project_name="myst",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="myst", addr=0xCFFE0)
