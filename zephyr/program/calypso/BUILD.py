# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for Calypso."""


def register_npcx9_project(
    project_name,
    extra_kconfig_files=(),
):
    """Register an npcx9 based variant of mensa."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7fb",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
    )


register_npcx9_project(
    project_name="mensa",
)

register_npcx9_project(
    project_name="c1nv",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="mensa", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="c1nv", addr=0x40144)
