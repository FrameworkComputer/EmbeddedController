# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for skywalker."""


def register_skywalker_npcx_project(project_name):
    """Register a variant of skywalker."""
    return register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7fb",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["skywalker"],
    )


def register_skywalker_ite_project(project_name):
    """Register a variant of skywalker using ITE EC."""
    return register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002aw",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / "ite_program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["skywalker"],
    )


register_skywalker_npcx_project(project_name="skywalker")
register_skywalker_ite_project(project_name="luuke")

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="skywalker", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="luuke", addr=0x60098)
