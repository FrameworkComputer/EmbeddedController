# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for fatcat."""


def register_npcx9_project(
    project_name,
    extra_kconfig_files=(),
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
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
    )


def register_it8xxx2_project(
    project_name,
    extra_kconfig_files=(),
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
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
    )


register_npcx9_project(
    project_name="fatcat_npcx9m7f",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
        here / ".." / "intelrvp" / "ptlrvp" / "pd.conf",
    ],
)

register_it8xxx2_project(
    project_name="fatcat_it82002aw",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
        here / ".." / "intelrvp" / "ptlrvp" / "pd.conf",
    ],
)

register_npcx9_project(
    project_name="francka",
)

register_it8xxx2_project(
    project_name="felino",
    extra_kconfig_files=[],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="fatcat_npcx9m7f", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="fatcat_it82002aw", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="francka", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="felino", addr=0x60098)
