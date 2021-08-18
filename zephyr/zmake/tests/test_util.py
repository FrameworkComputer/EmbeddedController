# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import tempfile

import hypothesis
import hypothesis.strategies as st
import pytest

import zmake.util as util

# Strategies for use with hypothesis
relative_path = st.from_regex(regex=r"\A\w+[\w/]*\Z")


@hypothesis.given(relative_path, relative_path, relative_path)
@hypothesis.settings(deadline=60000)
def test_resolve_build_dir_with_build_dir(
    platform_ec_subdir, project_subdir, build_subdir
):
    with tempfile.TemporaryDirectory() as temp_dir_name:
        platform_ec_dir = pathlib.Path(temp_dir_name) / platform_ec_subdir
        build_dir = util.resolve_build_dir(
            platform_ec_dir=platform_ec_dir,
            project_dir=platform_ec_dir / project_subdir,
            build_dir=platform_ec_dir / build_subdir,
        )

        assert build_dir == platform_ec_dir / build_subdir


@hypothesis.given(relative_path, relative_path)
@hypothesis.settings(deadline=60000)
def test_resolve_build_dir_invalid_project(platform_ec_subdir, project_subdir):
    try:
        with tempfile.TemporaryDirectory() as temp_dir_name:
            platform_ec_dir = pathlib.Path(temp_dir_name) / platform_ec_subdir
            util.resolve_build_dir(
                platform_ec_dir=platform_ec_dir,
                project_dir=platform_ec_dir / project_subdir,
                build_dir=None,
            )
            pytest.fail()
    except Exception:
        pass


@hypothesis.given(relative_path, relative_path)
@hypothesis.settings(deadline=60000)
def test_resolve_build_dir_from_project(platform_ec_subdir, project_subdir):
    with tempfile.TemporaryDirectory() as temp_dir_name:
        platform_ec_dir = pathlib.Path(temp_dir_name) / platform_ec_subdir
        project_dir = platform_ec_dir / project_subdir
        project_dir.mkdir(parents=True)
        (project_dir / "zmake.yaml").touch()
        build_dir = util.resolve_build_dir(
            platform_ec_dir=platform_ec_dir, project_dir=project_dir, build_dir=None
        )
        assert build_dir == platform_ec_dir / "build" / project_subdir


version_integers = st.integers(min_value=0)
version_tuples = st.tuples(version_integers, version_integers, version_integers)


@hypothesis.given(version_tuples)
@hypothesis.settings(deadline=60000)
def test_read_zephyr_version(version_tuple):
    with tempfile.TemporaryDirectory() as zephyr_base:
        with open(pathlib.Path(zephyr_base) / "VERSION", "w") as f:
            for name, value in zip(
                ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"), version_tuple
            ):
                f.write("{} = {}\n".format(name, value))

        assert util.read_zephyr_version(zephyr_base) == version_tuple


@hypothesis.given(st.integers())
@hypothesis.settings(deadline=60000)
def test_read_kconfig_autoconf_value(value):
    with tempfile.TemporaryDirectory() as dir:
        path = pathlib.Path(dir)
        with open(path / "autoconf.h", "w") as f:
            f.write("#define TEST {}".format(value))
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
    assert util.c_str(input_str) == expected_result
