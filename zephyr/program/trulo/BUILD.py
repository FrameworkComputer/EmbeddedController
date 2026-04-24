# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for trulo."""


def register_trulo_project(
    project_name,
    chip="npcx9/npcx9m3f",
    zephyr_board=None,
    kconfig_files=None,
    inherited_from=None,
    snippets=None,
    **kwargs,
):
    """Register a variant of Trulo."""
    if zephyr_board is None:
        zephyr_board = chip
    if inherited_from is None:
        inherited_from = ["nissa"]

    if "it8" in zephyr_board:
        register_binman_project(
            project_name=project_name,
            zephyr_board=zephyr_board,
            dts_overlays=[
                here / project_name / "project.overlay",
            ],
            kconfig_files=kconfig_files + [here / "dsp_comms.conf"],
            modules=["cmsis_6", "picolibc", "ec", "pigweed", "nanopb"],
            inherited_from=inherited_from,
            snippets=snippets,
            **kwargs,
        )
    else:
        register_npcx_project(
            project_name=project_name,
            zephyr_board=zephyr_board,
            dts_overlays=[
                here / project_name / "project.overlay",
            ],
            kconfig_files=kconfig_files + [here / "dsp_comms.conf"],
            inherited_from=inherited_from,
            modules=["cmsis_6", "picolibc", "ec", "pigweed", "nanopb"],
            snippets=snippets,
            **kwargs,
        )


def register_trulo_binman_project(
    project_name,
    kconfig_files=None,
    snippets=None,
):
    """Register a variant of trulo."""
    if kconfig_files is None:
        kconfig_files = [
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ]
    return register_trulo_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002aw",
        kconfig_files=kconfig_files,
        snippets=snippets,
    )


register_trulo_project(
    project_name="kaladin",
    zephyr_board="it8xxx2/it82002bw",
    kconfig_files=[
        # ite's config
        here / "ite.conf",
        # Common to all projects.
        here / "ite_program.conf",
        # Parent project's config
        here / "kaladin" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)

register_trulo_project(
    project_name="trulo",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "trulo" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)

register_trulo_project(
    project_name="pujjocento",
    chip="npcx9/npcx9m7fb",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "pujjocento" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)

register_trulo_project(
    project_name="pujjolo",
    chip="npcx9/npcx9m7fb",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "pujjolo" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)

register_trulo_project(
    project_name="trulo-ti",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "trulo" / "project.conf",
        # Project-specific KConfig customization.
        here / "trulo-ti" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)

register_trulo_project(
    project_name="uldrenite",
    chip="npcx9/npcx9m7fb",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "uldrenite" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)

register_ish_project(
    project_name="trulo-ish",
    zephyr_board="intel_ish_5_4_1",
    dts_overlays=[
        here / "trulo-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "trulo-ish" / "prj.conf",
        # Uncomment the following line for UART support
        # here / "trulo-ish" / "debug.conf",
        here / "dsp_comms.conf",
        here / ".." / ".." / "ish.conf",
    ],
    inherited_from=["nissa"],
)

register_ish_project(
    project_name="uldrenite-ish",
    zephyr_board="intel_ish_5_4_1",
    dts_overlays=[
        here / "uldrenite-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "uldrenite-ish" / "project.conf",
        # Uncomment the following line for UART support
        # here / "trulo-ish" / "debug.conf",
        here / "dsp_comms.conf",
        here / ".." / ".." / "ish.conf",
    ],
    inherited_from=["nissa"],
)

register_ish_project(
    project_name="pujjolo-ish",
    zephyr_board="intel_ish_5_4_1",
    dts_overlays=[
        here / "pujjolo-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "pujjolo-ish" / "project.conf",
        # Uncomment the following line for UART support
        # here / "pujjolo-ish" / "debug.conf",
        here / "dsp_comms.conf",
        here / ".." / ".." / "ish.conf",
    ],
    inherited_from=["nissa"],
)

register_ish_project(
    project_name="kaladin-ish",
    zephyr_board="intel_ish_5_4_1",
    dts_overlays=[
        here / "kaladin-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "kaladin-ish" / "project.conf",
        here / "dsp_comms.conf",
        here / ".." / ".." / "ish.conf",
    ],
    inherited_from=["nissa"],
)

register_ish_project(
    project_name="lite-ish",
    zephyr_board="intel_ish_5_4_1",
    dts_overlays=[
        here / "lite-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "lite-ish" / "project.conf",
        here / ".." / ".." / "ish.conf",
    ],
)
# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="trulo", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="pujjocento", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="pujjolo", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="trulo-ti", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="uldrenite", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="kaladin", addr=0x60098)
