# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for nissa."""

# Nivviks and Craask, Pujjo, Xivu has NPCX993F, Nereid and Joxer, Yaviks has ITE81302


def register_nissa_project(
    project_name,
    chip="it81302bx",
    extra_dts_overlays=(),
):
    """Register a variant of nissa."""
    register_func = register_binman_project
    if chip.startswith("npcx"):
        register_func = register_npcx_project

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=["cbi.dts"]
        + [here / project_name / filename for filename in extra_dts_overlays],
        kconfig_files=[here / "prj.conf", here / project_name / "prj.conf"],
    )


nivviks = register_nissa_project(
    project_name="nivviks",
    chip="npcx9m3f",
    extra_dts_overlays=[
        "generated.dts",
        "cbi.dts",
        "overlay.dts",
        "motionsense.dts",
        "keyboard.dts",
        "power_signals.dts",
        "pwm_leds.dts",
    ],
)

nereid = register_nissa_project(
    project_name="nereid",
    chip="it81302bx",
    extra_dts_overlays=[
        "generated.dts",
        "overlay.dts",
        "motionsense.dts",
        "keyboard.dts",
        "power_signals.dts",
        "pwm_leds.dts",
    ],
)

craask = register_nissa_project(
    project_name="craask",
    chip="npcx9m3f",
    extra_dts_overlays=[
        "generated.dts",
        "cbi.dts",
        "overlay.dts",
        "motionsense.dts",
        "keyboard.dts",
        "power_signals.dts",
        "pwm_leds.dts",
    ],
)

pujjo = register_nissa_project(
    project_name="pujjo",
    chip="npcx9m3f",
    extra_dts_overlays=[
        "generated.dts",
        "overlay.dts",
        "motionsense.dts",
        "keyboard.dts",
        "power_signals.dts",
    ],
)

xivu = register_nissa_project(
    project_name="xivu",
    chip="npcx9m3f",
    extra_dts_overlays=[
        "generated.dts",
        "cbi.dts",
        "overlay.dts",
        "motionsense.dts",
        "keyboard.dts",
        "power_signals.dts",
        "led_pins.dts",
        "led_policy.dts",
    ],
)

joxer = register_nissa_project(
    project_name="joxer",
    chip="it81302bx",
    extra_dts_overlays=[
        "generated.dts",
        "overlay.dts",
        "motionsense.dts",
        "keyboard.dts",
        "power_signals.dts",
        "pwm_leds.dts",
    ],
)

yaviks = register_nissa_project(
    project_name="yaviks",
    chip="it81302bx",
    extra_dts_overlays=[
        "gpio.dts",
        "overlay.dts",
        "keyboard.dts",
        "power_signals.dts",
        "pwm_leds.dts",
    ],
)
