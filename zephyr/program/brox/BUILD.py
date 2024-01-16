# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for brox."""


def register_brox_project(
    project_name,
    kconfig_files=None,
):
    """Register a variant of brox."""
    if kconfig_files is None:
        kconfig_files = [
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ]

    return register_binman_project(
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


brox = register_brox_project(
    project_name="brox",
)

register_brox_project(
    project_name="brox-ish-ec",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "brox" / "project.conf",
        # Project-specific KConfig customization.
        here / "brox-ish-ec" / "project.conf",
    ],
)

brox.variant(
    project_name="brox-tokenized",
    kconfig_files=[
        here / "brox-tokenized" / "project.conf",
    ],
    modules=["picolibc", "ec", "pigweed"],
)
