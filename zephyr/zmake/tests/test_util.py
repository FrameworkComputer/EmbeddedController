# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hypothesis
import hypothesis.strategies as st
import pathlib
import pytest

import zmake.util as util


# Strategies for use with hypothesis
absolute_path = st.from_regex(regex=r"\A/\w+[\w/]*\Z")
relative_path = st.from_regex(regex=r"\A\w+[\w/]*\Z")


@hypothesis.given(absolute_path, relative_path, relative_path)
def test_resolve_build_dir_with_build_dir(platform_ec_dir, project_subdir,
                                          build_subdir):
    platform_ec_dir = pathlib.PosixPath(platform_ec_dir)
    build_dir = util.resolve_build_dir(
        platform_ec_dir=platform_ec_dir,
        project_dir=platform_ec_dir / project_subdir,
        build_dir=platform_ec_dir / build_subdir)

    assert build_dir == platform_ec_dir / build_subdir


@hypothesis.given(absolute_path, relative_path)
def test_resolve_build_dir_default_dir(platform_ec_dir, project_subdir):
    platform_ec_dir = pathlib.PosixPath(platform_ec_dir)
    build_dir = util.resolve_build_dir(
        platform_ec_dir=platform_ec_dir,
        project_dir=platform_ec_dir / project_subdir,
        build_dir=None)
    assert build_dir == platform_ec_dir / 'build' / project_subdir


@hypothesis.given(absolute_path, relative_path)
def test_resolve_build_dir_not_platform_ec_subdir(root_dir, sub_dir):
    """In this case, the platform_ec_dir is a subdirectory of the project."""
    root_dir = pathlib.PosixPath(root_dir)
    try:
        util.resolve_build_dir(
            platform_ec_dir=root_dir / sub_dir,
            project_dir=root_dir,
            build_dir=None)
        pytest.fail()
    except Exception:
        pass
