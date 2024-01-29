# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for rauru."""


def register_rauru_project(project_name):
    """Register a variant of rauru."""
    register_func = register_binman_project

    return register_func(
        project_name=project_name,
        zephyr_board="it82202ax-512",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["rauru"],
    )


register_rauru_project(project_name="rauru")

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="rauru", addr=0x40098)
