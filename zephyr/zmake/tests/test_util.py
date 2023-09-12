# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake util."""

import pathlib
import tempfile

# pylint:disable=import-error
import hypothesis
import hypothesis.strategies as st
import pytest
from zmake import util


# Strategies for use with hypothesis
version_integers = st.integers(min_value=0)
version_tuples = st.tuples(version_integers, version_integers, version_integers)


@hypothesis.given(version_tuples)
@hypothesis.settings(deadline=60000)
def test_read_zephyr_version(version_tuple):
    """Test reading the zephyr version."""
    with tempfile.TemporaryDirectory() as zephyr_base:
        with open(
            pathlib.Path(zephyr_base) / "VERSION", "w", encoding="utf-8"
        ) as file:
            for name, value in zip(
                ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"), version_tuple
            ):
                file.write(f"{name} = {value}\n")

        assert util.read_zephyr_version(zephyr_base) == version_tuple


@hypothesis.given(st.integers())
@hypothesis.settings(deadline=60000)
def test_read_kconfig_autoconf_value(value):
    """Test reading the kconfig autoconf."""
    with tempfile.TemporaryDirectory() as temp_dir:
        path = pathlib.Path(temp_dir)
        with open(path / "autoconf.h", "w", encoding="utf-8") as file:
            file.write(f"#define TEST {value}")
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
