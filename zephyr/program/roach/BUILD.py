# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for roach."""


def register_variant(project_name, rwsig_sign=True):
    """Register a variant of Roach."""

    signer_kwarg = {}
    if rwsig_sign:
        # pylint: disable=undefined-variable
        signer_kwarg["signer"] = signers.RwsigSigner(here / "dev_key.pem")

    register_binman_project(
        project_name=project_name,
        zephyr_board="it82202ax-512",
        dts_overlays=[here / project_name / "project.overlay"],
        kconfig_files=[
            here / "program.conf",
            here / project_name / "project.conf",
        ],
        **signer_kwarg,
    )


register_variant("roach")
register_variant("axii", rwsig_sign=False)
register_variant("kelpie")
