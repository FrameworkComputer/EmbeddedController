# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for geralt."""


def register_geralt_project(
    project_name,
):
    """Register a variant of geralt."""
    return register_binman_project(
        project_name=project_name,
        zephyr_board="it81202cx",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
        inherited_from=["geralt"],
    )


geralt = register_geralt_project(project_name="geralt")
ciri = register_geralt_project(project_name="ciri")
