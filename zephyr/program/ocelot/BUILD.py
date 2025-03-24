# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for ocelot."""


def register_npcx9_project(
    project_name,
    extra_kconfig_files=(),
):
    """Register an npcx9 based variant of ocelot."""
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
    """Register an it8xxx2 based variant of ocelot."""
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


def register_mchp_project(
    project_name,
    extra_kconfig_files=(),
):
    """Register an microchip based variant of ocelot."""
    register_binman_project(
        project_name=project_name,
        zephyr_board="mec172x/mec172x_nsz/mec1727",
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
        modules=["cmsis", "ec"],
    )


# For use on SKU1 and SKU2
register_npcx9_project(
    project_name="ocelot_nuvoton",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
    ],
)

# For use on SKU3
register_it8xxx2_project(
    project_name="ocelot_ite",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
    ],
)

# For use on SKU4
register_mchp_project(
    project_name="ocelot_microchip",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
    ],
)


# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelot_nuvoton", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelot_ite", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelot_microchip", addr=0x40318)
