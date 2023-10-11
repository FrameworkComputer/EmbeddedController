# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for corsola."""

# Default chip is it81202bx, some variants will use NPCX9X.


def register_corsola_project(
    project_name,
    chip="it81202bx",
):
    """Register a variant of corsola."""
    register_func = register_binman_project
    if chip.startswith("npcx"):
        register_func = register_npcx_project

    chip_kconfig = {"it81202bx": "ite", "npcx9m3f": "npcx"}[chip]

    register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / f"{chip_kconfig}_program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["corsola"],
    )


def register_kingler_project(
    project_name,
):
    """Wrapper function for registering a variant of kingler."""
    return register_corsola_project(
        project_name=project_name,
        chip="npcx9m3f",
    )


def register_krabby_project(
    project_name,
):
    """Wrapper function for registering a variant of krabby."""
    return register_corsola_project(
        project_name=project_name,
        chip="it81202bx",
    )


register_corsola_project("krabby")

register_corsola_project(
    project_name="kingler",
    chip="npcx9m3f",
)

register_corsola_project(
    project_name="steelix",
    chip="npcx9m3f",
)

register_corsola_project("starmie")

register_corsola_project("tentacruel")

register_corsola_project("magikarp")

register_corsola_project(
    project_name="voltorb",
    chip="npcx9m3f",
)

register_corsola_project(
    project_name="ponyta",
    chip="npcx9m3f",
)

register_corsola_project("chinchou")
