# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake util."""

import pathlib
import tempfile

import hypothesis
import hypothesis.strategies as st
import pytest

import zmake.util as util

# Strategies for use with hypothesis
version_integers = st.integers(min_value=0)
version_tuples = st.tuples(version_integers, version_integers, version_integers)


@hypothesis.given(version_tuples)
@hypothesis.settings(deadline=60000)
def test_read_zephyr_version(version_tuple):
    """Test reading the zephyr version."""
    with tempfile.TemporaryDirectory() as zephyr_base:
        with open(pathlib.Path(zephyr_base) / "VERSION", "w") as file:
            for name, value in zip(
                ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"), version_tuple
            ):
                file.write("{} = {}\n".format(name, value))

        assert util.read_zephyr_version(zephyr_base) == version_tuple


@hypothesis.given(st.integers())
@hypothesis.settings(deadline=60000)
def test_read_kconfig_autoconf_value(value):
    """Test reading the kconfig autoconf."""
    with tempfile.TemporaryDirectory() as temp_dir:
        path = pathlib.Path(temp_dir)
        with open(path / "autoconf.h", "w") as file:
            file.write("#define TEST {}".format(value))
        read_value = util.read_kconfig_autoconf_value(path, "TEST")
        assert int(read_value) == value


@pytest.mark.parametrize(
    ["input_str", "expected_result"],
    [
        ("", '""'),
        ("TROGDOR ABC-123", '"TROGDOR ABC-123"'),
        ("hello world", '"hello world"'),
        ("hello\nworld", r'"hello\nworld"'),
        ('hello"world', r'"hello\"world"'),
        ("hello\\world", '"hello\\\\world"'),
    ],
)
def test_c_str(input_str, expected_result):
    """Test the util.c_str function."""
    assert util.c_str(input_str) == expected_result
