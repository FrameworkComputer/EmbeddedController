# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for nissa."""

# Nivviks and Craask, Pujjo, Xivu, Xivur, Uldren has NPCX993F, Nereid
# and Joxer, Yaviks, Yavilla, Quandiso has ITE81302


def register_nissa_project(
    project_name,
    chip="it81302bx",
):
    """Register a variant of nissa."""
    register_func = register_binman_project
    if chip.startswith("npcx"):
        register_func = register_npcx_project

    chip_kconfig = {"it81302bx": "it8xxx2", "npcx9m3f": "npcx"}[chip]

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / f"{chip_kconfig}_program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["nissa"],
    )


def register_nivviks_project(
    project_name,
):
    """Wrapper function for registering a variant of nivviks."""
    return register_nissa_project(
        project_name=project_name,
        chip="npcx9m3f",
    )


def register_nereid_project(
    project_name,
):
    """Wrapper function for registering a variant of nereid."""
    return register_nissa_project(
        project_name=project_name,
        chip="it81302bx",
    )


nivviks = register_nissa_project(
    project_name="nivviks",
    chip="npcx9m3f",
)

nereid = register_nissa_project(
    project_name="nereid",
    chip="it81302bx",
)

nereid_cx = register_binman_project(
    project_name="nereid_cx",
    zephyr_board="it81302cx",
    dts_overlays=[here / "nereid" / "project.overlay"],
    kconfig_files=[
        here / "program.conf",
        here / "it8xxx2_program.conf",
        here / "it8xxx2cx_program.conf",
        here / "nereid" / "project.conf",
    ],
)

nokris = register_nissa_project(
    project_name="nokris",
    chip="npcx9m3f",
)

craask = register_nissa_project(
    project_name="craask",
    chip="npcx9m3f",
)

pujjo = register_nissa_project(
    project_name="pujjo",
    chip="npcx9m3f",
)

xivu = register_nissa_project(
    project_name="xivu",
    chip="npcx9m3f",
)

xivur = register_nissa_project(
    project_name="xivur",
    chip="npcx9m3f",
)

joxer = register_nissa_project(
    project_name="joxer",
    chip="it81302bx",
)

yaviks = register_nissa_project(
    project_name="yaviks",
    chip="it81302bx",
)

yavilla = register_nissa_project(
    project_name="yavilla",
    chip="it81302bx",
)

uldren = register_nissa_project(
    project_name="uldren",
    chip="npcx9m3f",
)
gothrax = register_nissa_project(
    project_name="gothrax",
    chip="it81302bx",
)
craaskov = register_nissa_project(
    project_name="craaskov",
    chip="npcx9m3f",
)
pirrha = register_nissa_project(
    project_name="pirrha",
    chip="it81302bx",
)
quandiso = register_nissa_project(
    project_name="quandiso",
)
