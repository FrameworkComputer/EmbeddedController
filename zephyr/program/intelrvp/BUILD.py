# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for intelrvp."""

# intelrvp has adlrvp_npcx, adlrvpp_ite, adlrvpp_mchp etc


def register_intelrvp_project(
    project_name,
    chip,
    extra_dts_overlays=(),
    extra_kconfig_files=(),
    inherited_from=None,
):
    """Register a variant of intelrvp."""
    register_func = register_binman_project
    if chip.startswith("mec172"):
        register_func = register_mchp_project
    elif chip.startswith("npcx"):
        register_func = register_npcx_project

    kconfig_files = [here / "prj.conf"]
    dts_overlays = []
    if project_name.startswith("adlrvp"):
        kconfig_files.append(here / "adlrvp/prj.conf")
        dts_overlays.append(here / "adlrvp/battery.dts")
        dts_overlays.append(here / "adlrvp/ioex.dts")
    if project_name.startswith("mtlrvp"):
        kconfig_files.append(here / "mtlrvp/prj.conf")
        dts_overlays.append(here / "adlrvp/battery.dts")
    if project_name.startswith("ptlrvp"):
        kconfig_files.append(here / "ptlrvp/program.conf")
    kconfig_files.extend(extra_kconfig_files)
    dts_overlays.extend(extra_dts_overlays)

    if inherited_from is None:
        inherited_from = ["intelrvp"]

    register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=dts_overlays,
        kconfig_files=kconfig_files,
        inherited_from=inherited_from,
    )


register_intelrvp_project(
    project_name="adlrvp_mchp",
    chip="mec172x/mec172x_nsz/mec1727",
    extra_dts_overlays=[
        here / "adlrvp/adlrvp_mchp/adlrvp_mchp.dts",
        here / "adlrvp/adlrvp_mchp/gpio.dts",
        here / "adlrvp/adlrvp_mchp/interrupts.dts",
        here / "adlrvp/adlrvp_mchp/keyboard.dts",
        here / "adlrvp/adlrvp_mchp/usbc.dts",
    ],
    extra_kconfig_files=[
        here / "legacy_ec_pwrseq.conf",
        here / "adlrvp/adlrvp_mchp/prj.conf",
    ],
)


register_intelrvp_project(
    project_name="adlrvp_npcx",
    chip="npcx9/npcx9m7f",
    extra_dts_overlays=[
        here / "adlrvp/adlrvp_npcx/adlrvp_npcx.dts",
        here / "adlrvp/adlrvp_npcx/fan.dts",
        here / "adlrvp/adlrvp_npcx/gpio.dts",
        here / "adlrvp/adlrvp_npcx/interrupts.dts",
        here / "adlrvp/adlrvp_npcx/keyboard.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
        here / "adlrvp/adlrvp_npcx/usbc.dts",
        here / "adlrvp/adlrvp_npcx/pwm_leds.dts",
    ],
    extra_kconfig_files=[
        here / "legacy_ec_pwrseq.conf",
        here / "adlrvp/adlrvp_npcx/prj.conf",
    ],
)


register_intelrvp_project(
    project_name="mtlrvpp_m1723",
    chip="mec172x/mec172x_nsz/mec1723",
    extra_dts_overlays=[
        here / "mtlrvp/mtlrvpp_mchp/fan.dts",
        here / "mtlrvp/mtlrvpp_mchp/gpio.dts",
        here / "mtlrvp/mtlrvpp_mchp/keyboard.dts",
        here / "mtlrvp/mtlrvpp_npcx/interrupts.dts",
        here / "mtlrvp/mtlrvpp_npcx/tcpc_interrupts.dts",
        here / "mtlrvp/ioex.dts",
        here / "mtlrvp/tcpc_ioex.dts",
        here / "mtlrvp/mtlrvpp_mchp/mtlrvp_mchp.dts",
        here / "mtlrvp/mtlrvpp_m1723/mtlrvp_flash.dts",
        here / "mtlrvp/mtlrvpp_mchp/mtlrvp_mchp_power_signals.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
        here / "mtlrvp/usbc.dts",
    ],
    extra_kconfig_files=[
        here / "zephyr_ap_pwrseq.conf",
        here / "mtlrvp/mtlrvpp_m1723/prj.conf",
        here / "mtlrvp/mtlrvpp_mchp/board_mchp.conf",
        here / "mtlrvp/tcpc.conf",
    ],
)


register_intelrvp_project(
    project_name="mtlrvpp_mchp",
    chip="mec172x/mec172x_nsz/mec1727",
    extra_dts_overlays=[
        here / "mtlrvp/mtlrvpp_mchp/fan.dts",
        here / "mtlrvp/mtlrvpp_mchp/gpio.dts",
        here / "mtlrvp/mtlrvpp_mchp/keyboard.dts",
        here / "mtlrvp/mtlrvpp_npcx/interrupts.dts",
        here / "mtlrvp/mtlrvpp_npcx/tcpc_interrupts.dts",
        here / "mtlrvp/ioex.dts",
        here / "mtlrvp/tcpc_ioex.dts",
        here / "mtlrvp/mtlrvpp_mchp/mtlrvp_mchp.dts",
        here / "mtlrvp/mtlrvpp_mchp/mtlrvp_mchp_power_signals.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
        here / "mtlrvp/usbc.dts",
    ],
    extra_kconfig_files=[
        here / "zephyr_ap_pwrseq.conf",
        here / "mtlrvp/mtlrvpp_mchp/prj.conf",
        here / "mtlrvp/mtlrvpp_mchp/board_mchp.conf",
        here / "mtlrvp/tcpc.conf",
    ],
    inherited_from=["fatcat", "rex"],
)


register_intelrvp_project(
    project_name="mtlrvpp_npcx",
    chip="npcx9/npcx9m3f",
    extra_dts_overlays=[
        here / "mtlrvp/mtlrvpp_npcx/fan.dts",
        here / "mtlrvp/mtlrvpp_npcx/gpio.dts",
        here / "mtlrvp/mtlrvpp_npcx/tcpc_gpio.dts",
        here / "mtlrvp/mtlrvpp_npcx/keyboard.dts",
        here / "mtlrvp/mtlrvpp_npcx/interrupts.dts",
        here / "mtlrvp/mtlrvpp_npcx/tcpc_interrupts.dts",
        here / "mtlrvp/ioex.dts",
        here / "mtlrvp/tcpc_ioex.dts",
        here / "mtlrvp/mtlrvpp_npcx/mtlrvp_npcx.dts",
        here / "mtlrvp/mtlrvpp_npcx/tcpc_i2c.dts",
        here / "mtlrvp/mtlrvpp_npcx/mtlrvp_npcx_power_signals.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
        here / "mtlrvp/usbc.dts",
    ],
    extra_kconfig_files=[
        here / "zephyr_ap_pwrseq.conf",
        here / "mtlrvp/mtlrvpp_npcx/prj.conf",
        here / "mtlrvp/mtlrvpp_npcx/board_npcx.conf",
        here / "mtlrvp/tcpc.conf",
    ],
    inherited_from=["fatcat", "rex"],
)

register_intelrvp_project(
    project_name="mtlrvpp_pd",
    chip="npcx9/npcx9m3f",
    extra_dts_overlays=[
        here / "mtlrvp/mtlrvpp_npcx/fan.dts",
        here / "mtlrvp/mtlrvpp_npcx/gpio.dts",
        here / "mtlrvp/mtlrvpp_pd/gpio.dts",
        here / "mtlrvp/mtlrvpp_npcx/keyboard.dts",
        here / "mtlrvp/mtlrvpp_npcx/interrupts.dts",
        here / "mtlrvp/ioex.dts",
        here / "mtlrvp/mtlrvpp_npcx/mtlrvp_npcx.dts",
        here / "mtlrvp/mtlrvpp_pd/pd_i2c.dts",
        here / "mtlrvp/mtlrvpp_npcx/mtlrvp_npcx_power_signals.dts",
        here / "adlrvp/adlrvp_npcx/temp_sensor.dts",
        here / "mtlrvp/pd.dts",
    ],
    extra_kconfig_files=[
        here / "zephyr_ap_pwrseq.conf",
        here / "mtlrvp/mtlrvpp_pd/prj.conf",
        here / "mtlrvp/mtlrvpp_npcx/board_npcx.conf",
        here / "mtlrvp/pd.conf",
    ],
)
register_intelrvp_project(
    project_name="ptlrvp_mchp",
    chip="mec172x/mec172x_nsz/mec1727",
    extra_dts_overlays=[
        here / "ptlrvp/ptlrvp_mchp/project.overlay",
    ],
    extra_kconfig_files=[
        here / "ptlrvp/ptlrvp_mchp/project.conf",
        here / "ptlrvp/pd.conf",
        here / "zephyr_ap_pwrseq.conf",
    ],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="adlrvp_mchp", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="adlrvp_npcx", addr=0xCFFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="mtlrvpp_m1723", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="mtlrvpp_mchp", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="mtlrvpp_npcx", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="mtlrvpp_pd", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="ptlrvp_mchp", addr=0x7FFE0)
