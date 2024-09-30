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
        zephyr_board="it8xxx2/it82002aw",
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
        # Parent project's config
        here / "brox" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
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

brox_sku4 = register_brox_project(
    project_name="brox-sku4",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "brox" / "project.conf",
        # Project-specific KConfig customization.
        here / "brox-sku4" / "project.conf",
        # Common sensor configs
        here / "motionsense.conf",
    ],
)

brox.variant(
    project_name="brox-tokenized",
    kconfig_files=[
        here / "brox-tokenized" / "project.conf",
    ],
    modules=["picolibc", "ec", "pigweed"],
)

brox_sku4.variant(
    project_name="brox-sku4-tokenized",
    kconfig_files=[
        here / "brox-tokenized" / "project.conf",
    ],
    modules=["picolibc", "ec", "pigweed"],
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
    ],
)

greenbayupoc = register_brox_project(
    project_name="greenbayupoc",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "greenbayupoc" / "project.conf",
    ],
)

jubilant = register_brox_project(
    project_name="jubilant",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
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
assert_rw_fwid_DO_NOT_EDIT(project_name="brox-ish-ec", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="brox-tokenized", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="greenbayupoc", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="jubilant", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="lotso", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="brox-sku4", addr=0x70098)
assert_rw_fwid_DO_NOT_EDIT(project_name="brox-sku4-tokenized", addr=0x70098)
