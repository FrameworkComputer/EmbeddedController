# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for FPMCUs."""


def register_fpmcu_variant(
    project_name,
    zephyr_board,
    register_func,
    variant_modules=(),
    variant_optional_modules=(),
    variant_dts_overlays=(),
    variant_kconfig_files=(),
    signer=(),
):
    """Register an fpmcu variant"""
    return register_func(
        project_name=project_name,
        zephyr_board=zephyr_board,
        modules=["ec", *variant_modules],
        optional_modules=[*variant_optional_modules],
        supported_toolchains=["llvm", "zephyr"],
        dts_overlays=[*variant_dts_overlays],
        kconfig_files=[here / "prj.conf", *variant_kconfig_files],
        signer=signer,
    )


bloonchipper = register_fpmcu_variant(
    project_name="bloonchipper",
    zephyr_board="google_dragonclaw",
    register_func=register_binman_project,
    variant_modules=["hal_stm32", "cmsis"],
    variant_optional_modules=["fpc"],
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

helipilot = register_fpmcu_variant(
    project_name="helipilot",
    zephyr_board="google_quincy",
    register_func=register_npcx_project,
    variant_modules=["cmsis"],
    variant_optional_modules=["fpc"],
    variant_dts_overlays=[
        here / "helipilot" / "helipilot.dts",
    ],
    variant_kconfig_files=[
        here / "helipilot" / "prj.conf",
        here / "helipilot" / "ec_quirks.conf",
    ],
    signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
        here / "helipilot" / "dev_key.pem",
    ),
)

# The address of RW_FWID is hardcoded in RO. You need to have REALLY
# good reason to change it.
assert_rw_fwid_DO_NOT_EDIT(project_name="helipilot", addr=0x40144)
