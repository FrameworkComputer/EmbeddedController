# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hypothesis
import hypothesis.strategies as st
import pathlib
import pytest
import tempfile

import zmake.util as util


# Strategies for use with hypothesis
relative_path = st.from_regex(regex=r"\A\w+[\w/]*\Z")


@hypothesis.given(relative_path, relative_path, relative_path)
def test_resolve_build_dir_with_build_dir(platform_ec_subdir, project_subdir, build_subdir):
    with tempfile.TemporaryDirectory() as temp_dir_name:
        platform_ec_dir = pathlib.Path(temp_dir_name) / platform_ec_subdir
        (platform_ec_dir / build_subdir / 'project').mkdir(parents=True)
        (platform_ec_dir / build_subdir / 'project' / 'zmake.yaml').touch()
        build_dir = util.resolve_build_dir(
            platform_ec_dir=platform_ec_dir,
            project_dir=platform_ec_dir / project_subdir,
            build_dir=platform_ec_dir / build_subdir)

        assert build_dir == platform_ec_dir / build_subdir


@hypothesis.given(relative_path, relative_path)
def test_resolve_build_dir_invalid_project(platform_ec_subdir, project_subdir):
    try:
        with tempfile.TemporaryDirectory() as temp_dir_name:
            platform_ec_dir = pathlib.Path(temp_dir_name) / platform_ec_subdir
            util.resolve_build_dir(
                platform_ec_dir=platform_ec_dir,
                project_dir=platform_ec_dir / project_subdir,
                build_dir=None)
            pytest.fail()
    except Exception:
        pass


@hypothesis.given(relative_path, relative_path)
def test_resolve_build_dir_from_project(platform_ec_subdir, project_subdir):
    """Since the build dir is not a configured build directory but instead a
    project directory, it should be ignored.
    """
    with tempfile.TemporaryDirectory() as temp_dir_name:
        platform_ec_dir = pathlib.Path(temp_dir_name) / platform_ec_subdir
        project_dir = platform_ec_dir / project_subdir
        project_dir.mkdir(parents=True)
        (project_dir / 'zmake.yaml').touch()
        # Test when project_dir == build_dir.
        build_dir = util.resolve_build_dir(
            platform_ec_dir=platform_ec_dir,
            project_dir=project_dir,
            build_dir=project_dir)
        assert build_dir == platform_ec_dir / 'build' / project_subdir

        # Test when build_dir is None (it should be ignored).
        build_dir = util.resolve_build_dir(
            platform_ec_dir=platform_ec_dir,
            project_dir=project_dir,
            build_dir=None)
        assert build_dir == platform_ec_dir / 'build' / project_subdir
