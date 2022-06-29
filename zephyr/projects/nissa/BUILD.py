# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for nissa."""

# Nivviks and Craask, Pujjo, Xivu has NPCX993F, Nereid and Joxer has ITE81302


def register_nissa_project(
    project_name,
    chip="it8xxx2",
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    """Register a variant of nissa."""
    register_func = register_binman_project
    if chip.startswith("npcx"):
        register_func = register_npcx_project

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=["cbi.dts", *extra_dts_overlays],
        kconfig_files=[here / "prj.conf", *extra_kconfig_files],
    )


nivviks = register_nissa_project(
    project_name="nivviks",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "nivviks_generated.dts",
        here / "nivviks_cbi.dts",
        here / "nivviks_overlay.dts",
        here / "nivviks_motionsense.dts",
        here / "nivviks_keyboard.dts",
        here / "nivviks_power_signals.dts",
        here / "nivviks_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_nivviks.conf"],
)

nereid = register_nissa_project(
    project_name="nereid",
    chip="it8xxx2",
    extra_dts_overlays=[
        here / "nereid_generated.dts",
        here / "nereid_overlay.dts",
        here / "nereid_motionsense.dts",
        here / "nereid_keyboard.dts",
        here / "nereid_power_signals.dts",
        here / "nereid_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_nereid.conf"],
)

craask = register_nissa_project(
    project_name="craask",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "craask_generated.dts",
        here / "craask_overlay.dts",
        here / "craask_motionsense.dts",
        here / "craask_keyboard.dts",
        here / "craask_power_signals.dts",
        here / "craask_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_craask.conf"],
)

pujjo = register_nissa_project(
    project_name="pujjo",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "pujjo_generated.dts",
        here / "pujjo_overlay.dts",
        here / "pujjo_motionsense.dts",
        here / "pujjo_keyboard.dts",
        here / "pujjo_power_signals.dts",
        here / "pujjo_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_pujjo.conf"],
)

xivu = register_nissa_project(
    project_name="xivu",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "xivu_generated.dts",
        here / "xivu_overlay.dts",
        here / "xivu_motionsense.dts",
        here / "xivu_keyboard.dts",
        here / "xivu_power_signals.dts",
        here / "xivu_led_pins.dts",
        here / "xivu_led_policy.dts",
    ],
    extra_kconfig_files=[here / "prj_xivu.conf"],
)

joxer = register_nissa_project(
    project_name="joxer",
    chip="it8xxx2",
    extra_dts_overlays=[
        here / "joxer_generated.dts",
        here / "joxer_overlay.dts",
        here / "joxer_motionsense.dts",
        here / "joxer_keyboard.dts",
        here / "joxer_power_signals.dts",
        here / "joxer_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_joxer.conf"],
)
