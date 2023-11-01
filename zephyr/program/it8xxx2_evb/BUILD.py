# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for it8xxx2_evb."""


def register_it8xxx2_evb_project(project_name, zephyr_board):
    """Register a variant of the it8xxx2_evb"""
    register_raw_project(
        project_name=project_name,
        zephyr_board=zephyr_board,
        # Project-specific devicetree overlay
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        # Project-specific KConfig customization.
        kconfig_files=[
            here / project_name / "project.conf",
        ],
    )


register_it8xxx2_evb_project(
    project_name="it8xxx2_evb", zephyr_board="it81302bx"
)
