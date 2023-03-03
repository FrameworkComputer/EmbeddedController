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
        zephyr_board="npcx9m7f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
    )


register_myst_project(
    project_name="myst",
)
