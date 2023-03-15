# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for Rex."""


def register_variant(
    project_name,
    kconfig_files=None,
):
    """Register a variant of Rex."""
    if kconfig_files is None:
        kconfig_files = [
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ]

    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m7f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=kconfig_files,
    )


register_variant(
    project_name="rex",
)

register_variant(
    project_name="rex-sans-sensors",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "rex" / "project.conf",
        # Project-specific KConfig customization.
        here / "rex-sans-sensors" / "project.conf",
    ],
)
