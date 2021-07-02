# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import tempfile

import zmake.zmake as zm


def generate_zmake_yaml(dest, versions):
    with open(dest, "w") as f:
        f.write("board: test\n")
        f.write("output-type: elf\n")
        f.write("toolchain: llvm\n")
        f.write("supported-zephyr-versions:\n")
        for version in versions:
            f.write("  - {}\n".format(version))


def test_single_version(capsys):
    with tempfile.TemporaryDirectory() as temp_dir_name:
        generate_zmake_yaml(pathlib.Path(temp_dir_name) / "zmake.yaml", ["v2.5"])
        zm.Zmake().print_versions(temp_dir_name)
        captured = capsys.readouterr().out.splitlines()
        assert captured == ["v2.5"]


def test_multiple_versions(capsys):
    with tempfile.TemporaryDirectory() as temp_dir_name:
        generate_zmake_yaml(
            pathlib.Path(temp_dir_name) / "zmake.yaml", ["v2.5", "v2.6"]
        )
        zm.Zmake().print_versions(temp_dir_name)
        captured = capsys.readouterr().out.splitlines()
        # Compare the sorted lists since we don't guarantee anything about the
        # print order
        assert sorted(captured) == sorted(["v2.5", "v2.6"])
