# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Reworked drawcia with RISCV ITE EC

register_binman_project(
    project_name="drawcia_riscv",
    zephyr_board="it8xxx2",
    dts_overlays=["gpio.dts", "kb.dts"],
)
