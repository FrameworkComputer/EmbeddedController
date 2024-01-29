# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for lazor."""

register_npcx_project(
    project_name="lazor",
    zephyr_board="npcx7",
    dts_overlays=[
        "adc.dts",
        "battery.dts",
        "display.dts",
        "gpio.dts",
        "i2c.dts",
        "host_interface_npcx.dts",
        "interrupts.dts",
        "keyboard.dts",
        "led.dts",
        "motionsense.dts",
        "pwm_led.dts",
        "usbc.dts",
        "default_gpio_pinctrl.dts",
    ],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="lazor", addr=0x7FFE0)
