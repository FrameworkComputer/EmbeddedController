# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for tanjiro."""


def register_tanjiro_project(project_name):
    """Register a variant of tanjiro."""

    return register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82202bw",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["tanjiro"],
    )


register_tanjiro_project(project_name="tanjiro")

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="tanjiro", addr=0x60098)
