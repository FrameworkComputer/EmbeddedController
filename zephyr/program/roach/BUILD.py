# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for roach."""


def register_variant(project_name):
    """Register a variant of Roach."""
    register_binman_project(
        project_name=project_name,
        zephyr_board="it82202ax-512",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
        signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
            here / "dev_key.pem"
        ),
    )


register_variant("roach")
