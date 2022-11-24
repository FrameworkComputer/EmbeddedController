# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for Rex."""


def register_variant(
    project_name,
):
    """Register a variant of Rex."""
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


register_variant(
    project_name="rex",
)
