# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for Rex."""


def register_rex_project(
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
        inherited_from=["rex"],
    )


register_rex_project(
    project_name="rex",
)

register_rex_project(
    project_name="rex-ish-ec",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "rex" / "project.conf",
        # Project-specific KConfig customization.
        here / "rex-ish-ec" / "project.conf",
    ],
)

register_rex_project(
    project_name="screebo",
)
register_rex_project(
    project_name="karis",
)

register_ish_project(
    project_name="rex-ish",
    zephyr_board="intel_ish_5_6_0",
    dts_overlays=[
        here / "rex-ish" / "project.overlay",
    ],
    kconfig_files=[here / "rex-ish" / "prj.conf"],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="screebo", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="karis", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="rex", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="rex-ish-ec", addr=0x80144)
