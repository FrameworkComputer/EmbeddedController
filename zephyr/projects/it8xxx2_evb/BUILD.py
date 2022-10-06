# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for it8xxx2_evb."""

register_raw_project(
    project_name="it8xxx2_evb",
    zephyr_board="it81302bx",
    dts_overlays=[
        "adc.dts",
        "fan.dts",
        "gpio.dts",
        "i2c.dts",
        "interrupts.dts",
        "pwm.dts",
    ],
)
