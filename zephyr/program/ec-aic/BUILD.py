# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""EC-AIC projects."""


def register_ite_project(
    project_name,
    extra_kconfig_files=(),
    extra_dt_overlays=(),
):
    """Register an ITE variant of ec-aic."""

    return register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002aw",
        dts_overlays=[
            here / "ite-aic" / "project.overlay",
            *extra_dt_overlays,
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / "ite-aic" / "project.conf",
            *extra_kconfig_files,
        ],
        inherited_from=["ec-aic"],
    )


def register_nuvoton_project(
    project_name,
    extra_kconfig_files=(),
    extra_dt_overlays=(),
):
    """Register an Nuvoton variant of ec-aic."""

    return register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7f",
        dts_overlays=[
            here / "npcx-aic" / "project.overlay",
            *extra_dt_overlays,
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / "npcx-aic" / "project.conf",
            *extra_kconfig_files,
        ],
        inherited_from=["ec-aic"],
    )


def register_realtek_project(
    project_name,
    extra_kconfig_files=(),
    extra_dt_overlays=(),
):
    """Register an Realtek variant of ec-aic."""

    return register_rtk_project(
        project_name=project_name,
        zephyr_board="realtek/rts5912",
        dts_overlays=[
            here / "rtk-aic" / "project.overlay",
            *extra_dt_overlays,
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / "rtk-aic" / "project.conf",
            *extra_kconfig_files,
        ],
        inherited_from=["ec-aic"],
    )


# Base ITE project. This build assumes no additional hardware attached to the
# AIC and serves as a minimal EC build.
aic_ite = register_ite_project(
    project_name="ite-aic",
)

# ITE + RTK PDC project. Assumes a Realtek PDC evaluation board is connected to
# the AIC. Enables the PDC software stack and relevant devicetree nodes for two
# USB-C ports, plus dependencies (e.g. charger)
#
# Connect the RTK evaluation board (EVB) as follows:
#  * RTK SMBus (J4 "GPIO5" is SCL, J4 "GPIO6" is SDA) to AIC I2C2 (J12)
#    * Note: I2C2 is 3.3V levels.
#  * RTK EVB IRQ (J4 "GPIO4") to AIC J22.3 (EC GPIO E5)
#  * Connect GND
aic_ite_rtk_pdc = register_ite_project(
    project_name="ite-aic-rtk-pdc",
    extra_kconfig_files=(here / "ite-aic" / "project_pdc.conf",),
    extra_dt_overlays=(
        here / "ite-aic" / "project_pdc.overlay",
        here / "ite-aic" / "project_pdc_rtk.overlay",
    ),
)

# Base NPCX (Nuvoton) project. This build assumes no additional hardware
# attached to the AIC and serves as a minimal EC build.
npcx_aic = register_nuvoton_project(
    project_name="npcx-aic",
)

# Base RTS (Realtek) project. This build assumes no additional hardware
# attached to the AIC and serves as a minimal EC build.
rtk_aic = register_realtek_project(
    project_name="rtk-aic",
)

assert_rw_fwid_DO_NOT_EDIT(project_name="ite-aic", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="ite-aic-rtk-pdc", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="npcx-aic", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="rtk-aic", addr=0xCFFE0)
