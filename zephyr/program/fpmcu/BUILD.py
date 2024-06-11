# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for FPMCUs."""

import os


def register_fpmcu_variant(
    project_name,
    zephyr_board,
    variant_modules=(),
    variant_dts_overlays=(),
    variant_kconfig_files=(),
    signer=(),
):
    """Register FPMCU project"""
    return register_binman_project(
        project_name=project_name,
        zephyr_board=zephyr_board,
        modules=["ec", *variant_modules],
        supported_toolchains=["llvm", "zephyr"],
        dts_overlays=[*variant_dts_overlays],
        kconfig_files=[here / "prj.conf", *variant_kconfig_files],
        signer=signer,
    )


FPC_EXISTS = os.path.exists(
    os.path.join(os.getcwd(), os.pardir, "fingerprint/fpc")
)

variant_modules_list = ["hal_stm32", "cmsis"]

if FPC_EXISTS:
    variant_modules_list.append("fpc")


bloonchipper = register_fpmcu_variant(
    project_name="bloonchipper",
    zephyr_board="google_dragonclaw",
    variant_modules=variant_modules_list,
    variant_dts_overlays=[
        here / "bloonchipper" / "bloonchipper.dts",
        here / "bloonchipper" / "ec_quirks.dts",
    ],
    variant_kconfig_files=[
        here / "bloonchipper" / "prj.conf",
        here / "bloonchipper" / "ec_quirks.conf",
    ],
    signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
        here / "bloonchipper" / "dev_key.pem",
    ),
)

# The address of RW_FWID is hardcoded in RO. You need to have REALLY
# good reason to change it.
assert_rw_fwid_DO_NOT_EDIT(project_name="bloonchipper", addr=0x601C8)
