# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Minimal example project."""

register_host_project(
    project_name="minimal-posix",
    zephyr_board="native_posix",
)

register_npcx_project(
    project_name="minimal-npcx9",
    zephyr_board="npcx9/npcx9m3f",
    dts_overlays=[here / "npcx9.dts"],
)

register_binman_project(
    project_name="minimal-it8xxx2",
    zephyr_board="it8xxx2/it81302bx",
    dts_overlays=[here / "it8xxx2.dts"],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="minimal-it8xxx2", addr=0xBFFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="minimal-npcx9", addr=0x7FFE0)
