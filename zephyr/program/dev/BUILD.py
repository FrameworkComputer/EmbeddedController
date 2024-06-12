# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Project used for Zephyr development."""

register_host_project(
    project_name="dev-posix",
    zephyr_board="native_sim",
    dts_overlays=[
        "dev.dtsi",
    ],
)
