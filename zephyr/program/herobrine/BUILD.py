# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for herobrine."""


def register_variant(
    project_name,
):
    """Register a variant of herobrine."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m3f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
        inherited_from=["herobrine"],
    )


register_variant(
    project_name="evoker",
)

register_variant(
    project_name="herobrine",
)

register_variant(
    project_name="hoglin",
)

register_variant(
    project_name="villager",
)

register_variant(
    project_name="zoglin",
)

register_variant(
    project_name="zombie",
)
