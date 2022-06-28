# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Minimal example project."""

register_host_project(
    project_name="minimal-posix",
    zephyr_board="native_posix",
)

register_npcx_project(
    project_name="minimal-npcx9",
    zephyr_board="npcx9m3f",
    dts_overlays=[here / "npcx9.dts"],
)

register_binman_project(
    project_name="minimal-it8xxx2",
    zephyr_board="it8xxx2",
    dts_overlays=[here / "it8xxx2.dts"],
)
