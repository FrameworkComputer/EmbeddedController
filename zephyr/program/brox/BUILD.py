# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for brox."""


def register_brox_project(
    project_name,
    chip="it8xxx2/it82002aw",
    kconfig_files=None,
):
    """Register a variant of brox."""
    register_func = register_binman_project
    if chip.startswith("realtek"):
        register_func = register_rtk_project

    if kconfig_files is None:
        kconfig_files = [
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ]

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=kconfig_files,
        inherited_from=["brox"],
    )


brox = register_brox_project(
    project_name="brox",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Parent project's config
        here / "brox" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
)

brtk = register_brox_project(
    project_name="brtk",
    chip="realtek/rts5912",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "brtk" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
)

register_brox_project(
    project_name="brox-ish-ec",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Parent project's config
        here / "brox" / "project.conf",
        # Project-specific KConfig customization.
        here / "brox-ish-ec" / "project.conf",
    ],
)

brox_sku4 = register_brox_project(
    project_name="brox-sku4",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Parent project's config
        here / "brox" / "project.conf",
        # Project-specific KConfig customization.
        here / "brox-sku4" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
)

register_ish_project(
    project_name="brox-ish",
    zephyr_board="intel_ish_5_4_1",
    dts_overlays=[
        here / "brox-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "brox-ish" / "prj.conf",
        here / "motionsense.conf",
        here / ".." / ".." / "ish.conf",
    ],
    inherited_from=["brox"],
)

caboc = register_brox_project(
    project_name="caboc",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Parent project's config
        here / "caboc" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
)

greenbayupoc = register_brox_project(
    project_name="greenbayupoc",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Parent project's config
        here / "greenbayupoc" / "project.conf",
    ],
)

juchi = register_brox_project(
    project_name="juchi",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Common sensor configs
        here / "motionsense.conf",
        # Project-specific config
        here / "juchi" / "project.conf",
    ],
)

jubilant = register_brox_project(
    project_name="jubilant",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Project-specific config
        here / "jubilant" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
)

lotso = register_brox_project(
    project_name="lotso",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # ite's config
        here / "ite.conf",
        # Common sensor configs
        here / "motionsense.conf",
        # Project-specific config
        here / "lotso" / "project.conf",
    ],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release. Not needed for brox-ish since it
# doesn't use RO+RW
assert_rw_fwid_DO_NOT_EDIT(project_name="brox", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="brtk", addr=0x80404)
assert_rw_fwid_DO_NOT_EDIT(project_name="brox-ish-ec", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="caboc", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="greenbayupoc", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="juchi", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="jubilant", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="lotso", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="brox-sku4", addr=0x70098)
