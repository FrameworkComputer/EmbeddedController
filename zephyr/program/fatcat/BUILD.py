# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for fatcat."""


def register_npcx9_project(
    project_name,
):
    """Register an npcx9 based variant of fatcat."""
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
    )


def register_it8xxx2_project(
    project_name,
):
    """Register an it8xxx2 based variant of fatcat."""
    register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002aw",
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


register_npcx9_project(
    project_name="fatcat_npcx9m7f",
)

register_it8xxx2_project(
    project_name="fatcat_it82002aw",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="fatcat_npcx9m7f", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="fatcat_it82002aw", addr=0x60098)
