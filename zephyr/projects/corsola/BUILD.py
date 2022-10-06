# Copyright 2021 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for corsola."""

# Default chip is it81202bx, some variants will use NPCX9X.


def register_corsola_project(
    project_name,
    chip="it81202bx",
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    """Register a variant of corsola."""
    register_func = register_binman_project
    if chip.startswith("npcx"):
        register_func = register_npcx_project

    register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[
            here / "common.dts",
            here / "power_signal.dts",
            here / "usba.dts",
            *extra_dts_overlays,
        ],
        kconfig_files=[here / "prj.conf", *extra_kconfig_files],
    )


register_corsola_project(
    "krabby",
    extra_dts_overlays=[
        here / "adc_krabby.dts",
        here / "battery_krabby.dts",
        here / "gpio_krabby.dts",
        here / "i2c_krabby.dts",
        here / "interrupts_krabby.dts",
        here / "led_krabby.dts",
        here / "motionsense_krabby.dts",
        here / "usbc_krabby.dts",
    ],
    extra_kconfig_files=[
        here / "prj_it81202_base.conf",
        here / "prj_krabby.conf",
    ],
)

register_corsola_project(
    project_name="kingler",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "adc_kingler.dts",
        here / "battery_kingler.dts",
        here / "host_interface_npcx.dts",
        here / "i2c_kingler.dts",
        here / "interrupts_kingler.dts",
        here / "gpio_kingler.dts",
        here / "npcx_keyboard.dts",
        here / "led_kingler.dts",
        here / "motionsense_kingler.dts",
        here / "usbc_kingler.dts",
        here / "default_gpio_pinctrl_kingler.dts",
    ],
    extra_kconfig_files=[
        here / "prj_npcx993_base.conf",
        here / "prj_kingler.conf",
    ],
)

register_corsola_project(
    project_name="steelix",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "adc_kingler.dts",
        here / "battery_steelix.dts",
        here / "host_interface_npcx.dts",
        here / "i2c_kingler.dts",
        here / "interrupts_kingler.dts",
        here / "interrupts_steelix.dts",
        here / "cbi_steelix.dts",
        here / "gpio_steelix.dts",
        here / "npcx_keyboard.dts",
        here / "keyboard_steelix.dts",
        here / "led_steelix.dts",
        here / "motionsense_kingler.dts",
        here / "motionsense_steelix.dts",
        here / "usba_steelix.dts",
        here / "usbc_kingler.dts",
        here / "default_gpio_pinctrl_kingler.dts",
    ],
    extra_kconfig_files=[
        here / "prj_npcx993_base.conf",
        here / "prj_steelix.conf",
    ],
)


register_corsola_project(
    "tentacruel",
    extra_dts_overlays=[
        here / "adc_tentacruel.dts",
        here / "battery_tentacruel.dts",
        here / "cbi_tentacruel.dts",
        here / "gpio_tentacruel.dts",
        here / "i2c_tentacruel.dts",
        here / "interrupts_tentacruel.dts",
        here / "led_tentacruel.dts",
        here / "motionsense_tentacruel.dts",
        here / "usbc_tentacruel.dts",
        here / "thermistor_tentacruel.dts",
    ],
    extra_kconfig_files=[
        here / "prj_it81202_base.conf",
        here / "prj_tentacruel.conf",
    ],
)

register_corsola_project(
    "magikarp",
    extra_dts_overlays=[
        here / "adc_magikarp.dts",
        here / "battery_magikarp.dts",
        here / "cbi_magikarp.dts",
        here / "gpio_magikarp.dts",
        here / "i2c_magikarp.dts",
        here / "interrupts_magikarp.dts",
        here / "led_magikarp.dts",
        here / "motionsense_magikarp.dts",
        here / "usbc_magikarp.dts",
    ],
    extra_kconfig_files=[
        here / "prj_it81202_base.conf",
        here / "prj_magikarp.conf",
    ],
)
