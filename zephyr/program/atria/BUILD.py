# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for atria."""


def register_rtk59_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
):
    """Register a Realtek-based variant of atria."""
    register_rtk_project(
        project_name=project_name,
        zephyr_board="realtek/rts5912",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
    )


# Realtek RVP SKU
register_rtk59_project(
    project_name="atriarvp-rtk",
)


register_ish_project(
    project_name="atriarvp-ish",
    zephyr_board="intel_ish_5_8_0",
    dts_overlays=[
        here / "atriarvp-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "atriarvp-ish" / "project.conf",
        here / ".." / ".." / "ish.conf",
    ],
)


# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="atriarvp-rtk", addr=0x80404)
