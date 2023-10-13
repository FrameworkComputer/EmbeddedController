# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for brox."""


def register_brox_project(
    project_name,
):
    """Register a variant of brox."""
    register_binman_project(
        project_name=project_name,
        zephyr_board="it82002aw",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
        inherited_from=["brox"],
    )


register_brox_project(
    project_name="brox",
)
