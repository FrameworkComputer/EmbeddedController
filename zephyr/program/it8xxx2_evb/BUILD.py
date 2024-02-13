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
    project_name="it8xxx2_evb", zephyr_board="it8xxx2/it81302bx"
)

# The it82002_evb board consists of the it82002 BGA board connected
# to the DK-IT51300-4N-S01A breakout board.
#
# Download the ec.bin using ITE's download utility.  The source for
# the utility can be found at
# https://www.ite.com.tw/uploads/product_download/itedlb4-linux-v106.tar.bz2
#
# It is recommended to install the ITE tool under your home directory
#
# Build and download instructions:
#
#   zmake build it82002_evb
#   itedlb4-linux-v106/ite -f build/zephyr/it82002_evb/output/ec.bin
#
register_it8xxx2_evb_project(
    project_name="it82002_evb", zephyr_board="it8xxx2/it82002aw"
)
