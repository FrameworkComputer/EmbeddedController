# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import pathlib
import string
import tempfile

import hypothesis
import hypothesis.strategies as st
import pytest

import zmake.jobserver
import zmake.util as util
from zmake.build_config import BuildConfig

# Strategies for use with hypothesis
filenames = st.text(
    alphabet=set(string.printable) - {"/", ";"}, min_size=1, max_size=254
).filter(lambda name: name not in (".", ".."))
paths = st.builds(
    lambda parts: pathlib.Path("/", *parts), st.iterables(filenames, min_size=1)
)
config_keys = st.text(alphabet=set(string.ascii_uppercase) | {"_"}, min_size=1)
config_values = st.builds(str, st.just("y") | st.just("n") | st.integers())
config_dicts = st.dictionaries(keys=config_keys, values=config_values)
config_dicts_at_least_one_entry = st.dictionaries(
    keys=config_keys, values=config_values, min_size=1
)

build_configs = st.builds(
    BuildConfig,
    environ_defs=config_dicts,
    cmake_defs=config_dicts,
    kconfig_defs=config_dicts,
    kconfig_files=st.lists(paths),
)
build_configs_no_kconfig = st.builds(
    BuildConfig, environ_defs=config_dicts, cmake_defs=config_dicts
)
build_configs_with_at_least_one_kconfig = st.builds(
    BuildConfig,
    environ_defs=config_dicts,
    cmake_defs=config_dicts,
    kconfig_defs=config_dicts_at_least_one_entry,
)


@hypothesis.given(st.data(), build_configs)
def test_merge(coins, combined):
    """Test that when splitting a config in half and merging the two
    halves, we get the original config back.
    """

    def split(iterable):
        left = []
        right = []
        bools = st.booleans()
        for item in iterable:
            if coins.draw(bools):
                left.append(item)
            else:
                right.append(item)
        return left, right

    # Split the original config into two
    env1, env2 = split(combined.environ_defs.items())
    cmake1, cmake2 = split(combined.cmake_defs.items())
    kconf1, kconf2 = split(combined.kconfig_defs.items())
    files1, files2 = split(combined.kconfig_files)

    c1 = BuildConfig(
        environ_defs=dict(env1),
        cmake_defs=dict(cmake1),
        kconfig_defs=dict(kconf1),
        kconfig_files=files1,
    )
    c2 = BuildConfig(
        environ_defs=dict(env2),
        cmake_defs=dict(cmake2),
        kconfig_defs=dict(kconf2),
        kconfig_files=files2,
    )

    # Merge the split configs
    merged = c1 | c2

    # Assert that the merged split configs is the original config
    assert merged.environ_defs == combined.environ_defs
    assert merged.cmake_defs == combined.cmake_defs
    assert merged.kconfig_defs == combined.kconfig_defs
    assert set(merged.kconfig_files) == set(combined.kconfig_files)


class FakeJobClient(zmake.jobserver.JobClient):
    """Simple job client to capture argv/environ."""

    def __init__(self):
        self.captured_argv = []
        self.captured_env = {}

    def get_job(self):
        return zmake.jobserver.JobHandle(lambda: None)

    def popen(self, argv, env={}, **kwargs):
        self.captured_argv = [str(arg) for arg in argv]
        self.captured_env = {str(k): str(v) for k, v in env.items()}


def parse_cmake_args(argv):
    """Parse command line arguments like cmake does.

    This is an intenionally minimal implementation, which only
    understands the subset of arguments actually used by zmake.

    Args:
        argv: The argument list.

    Returns:
        A 2-tuple of a namespace from argparse and the corresponding
        parsed Cmake definitions.
    """
    assert argv[0] == "/usr/bin/cmake"

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("-S", dest="source_dir", type=pathlib.Path)
    parser.add_argument("-B", dest="build_dir", type=pathlib.Path)
    parser.add_argument("-G", dest="generator")
    parser.add_argument("-D", dest="defs", action="append", default=[])
    args = parser.parse_args(argv[1:])

    # Build the definition dictionary
    cmake_defs = {}
    for defn in args.defs:
        key, sep, val = defn.partition("=")
        if not sep:
            val = "1"
        assert key not in cmake_defs
        cmake_defs[key] = val

    return args, cmake_defs


@hypothesis.given(build_configs_no_kconfig, paths, paths)
@hypothesis.settings(deadline=60000)
def test_popen_cmake_no_kconfig(conf, project_dir, build_dir):
    """Test popen_cmake for a config with no kconfig definitions."""
    job_client = FakeJobClient()
    conf.popen_cmake(job_client, project_dir, build_dir)

    args, cmake_defs = parse_cmake_args(job_client.captured_argv)

    assert cmake_defs == conf.cmake_defs
    assert job_client.captured_env == conf.environ_defs


@hypothesis.given(build_configs_with_at_least_one_kconfig, paths, paths)
@hypothesis.settings(deadline=60000)
def test_popen_cmake_kconfig_but_no_file(conf, project_dir, build_dir):
    """Test that running popen_cmake with Kconfig definitions to write
    out, but no path to do so, should raise an error.
    """
    job_client = FakeJobClient()

    with pytest.raises(ValueError):
        conf.popen_cmake(job_client, project_dir, build_dir)


@hypothesis.given(build_configs, paths, paths)
@hypothesis.settings(deadline=60000)
def test_popen_cmake_kconfig(conf, project_dir, build_dir):
    job_client = FakeJobClient()

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        temp_path = f.name

    try:
        conf.popen_cmake(
            job_client, project_dir, build_dir, kconfig_path=pathlib.Path(temp_path)
        )

        args, cmake_defs = parse_cmake_args(job_client.captured_argv)

        expected_kconfig_files = set(str(f) for f in conf.kconfig_files)
        expected_kconfig_files.add(temp_path)

        if expected_kconfig_files:
            kconfig_files = set(cmake_defs.pop("CONF_FILE").split(";"))
        else:
            assert "CONF_FILE" not in cmake_defs
            kconfig_files = set()

        assert cmake_defs == conf.cmake_defs
        assert job_client.captured_env == conf.environ_defs
        assert kconfig_files == expected_kconfig_files

        kconfig_defs = util.read_kconfig_file(temp_path)
        assert kconfig_defs == conf.kconfig_defs
    finally:
        os.unlink(temp_path)


def test_build_config_json_stability():
    # as_json() should return equivalent strings for two equivalent
    # build configs.
    a = BuildConfig(
        environ_defs={
            "A": "B",
            "B": "C",
        },
        cmake_defs={
            "Z": "Y",
            "X": "W",
        },
        kconfig_defs={
            "CONFIG_A": "y",
            "CONFIG_B": "n",
        },
        kconfig_files=[
            pathlib.Path("/a/b/c.conf"),
            pathlib.Path("d/e/f.conf"),
        ],
    )

    # Dict ordering is intentionally reversed in b.
    b = BuildConfig(
        environ_defs={
            "B": "C",
            "A": "B",
        },
        cmake_defs={
            "X": "W",
            "Z": "Y",
        },
        kconfig_defs={
            "CONFIG_B": "n",
            "CONFIG_A": "y",
        },
        kconfig_files=[
            pathlib.Path("/a/b/c.conf"),
            pathlib.Path("d/e/f.conf"),
        ],
    )

    assert a.as_json() == b.as_json()


def test_build_config_json_inequality():
    # Two differing build configs should not have the same json
    # representation.
    a = BuildConfig(cmake_defs={"A": "B"})
    b = BuildConfig(environ_defs={"A": "B"})

    assert a.as_json() != b.as_json()
