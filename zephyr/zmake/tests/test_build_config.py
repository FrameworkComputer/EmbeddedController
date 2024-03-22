# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests of zmake's build config system."""

import argparse
import os
import pathlib
import string
import tempfile

# pylint: disable=import-error
import hypothesis
import hypothesis.strategies as st
import pytest
from zmake import util
from zmake.build_config import BuildConfig
import zmake.jobserver


# pylint:disable=redefined-outer-name,unused-argument

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
    cmake_defs=config_dicts,
    kconfig_defs=config_dicts,
    kconfig_files=st.lists(paths),
)
build_configs_no_kconfig = st.builds(BuildConfig, cmake_defs=config_dicts)
build_configs_with_at_least_one_kconfig = st.builds(
    BuildConfig,
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
    cmake1, cmake2 = split(combined.cmake_defs.items())
    kconf1, kconf2 = split(combined.kconfig_defs.items())
    files1, files2 = split(combined.kconfig_files)

    config1 = BuildConfig(
        cmake_defs=dict(cmake1),
        kconfig_defs=dict(kconf1),
        kconfig_files=files1,
    )
    config2 = BuildConfig(
        cmake_defs=dict(cmake2),
        kconfig_defs=dict(kconf2),
        kconfig_files=files2,
    )

    # Merge the split configs
    merged = config1 | config2

    # Assert that the merged split configs is the original config
    assert merged.cmake_defs == combined.cmake_defs
    assert merged.kconfig_defs == combined.kconfig_defs
    assert set(merged.kconfig_files) == set(combined.kconfig_files)


class FakeJobClient(zmake.jobserver.JobClient):
    """Simple job client to capture argv/environ."""

    def __init__(self):
        self.captured_argv = []
        self.captured_env = {}

    def get_job(self):  # pylint: disable=no-self-use
        """See base class."""
        return zmake.jobserver.JobHandle(lambda: None)

    def popen(self, argv, **kwargs):
        """See base class."""
        kwargs.setdefault("env", {})
        self.captured_argv = [str(arg) for arg in argv]
        self.captured_env = {str(k): str(v) for k, v in kwargs["env"].items()}


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

    build_cfg = BuildConfig.from_args(args.defs)
    return args, build_cfg.cmake_defs


@hypothesis.given(build_configs_no_kconfig, paths, paths)
@hypothesis.settings(deadline=60000)
def test_popen_cmake_no_kconfig(conf: BuildConfig, project_dir, build_dir):
    """Test popen_cmake for a config with no kconfig definitions."""
    job_client = FakeJobClient()
    conf.popen_cmake(job_client, project_dir, build_dir)

    _, cmake_defs = parse_cmake_args(job_client.captured_argv)

    assert cmake_defs == conf.cmake_defs


@hypothesis.given(build_configs_with_at_least_one_kconfig, paths, paths)
@hypothesis.settings(deadline=60000)
def test_popen_cmake_kconfig_but_no_file(
    conf: BuildConfig, project_dir, build_dir
):
    """Test that running popen_cmake with Kconfig definitions to write
    out, but no path to do so, should raise an error.
    """
    job_client = FakeJobClient()

    with pytest.raises(ValueError):
        conf.popen_cmake(job_client, project_dir, build_dir)


@hypothesis.given(build_configs, paths, paths)
@hypothesis.settings(deadline=60000)
def test_popen_cmake_kconfig(conf: BuildConfig, project_dir, build_dir):
    """Test calling popen_cmake and verifying the kconfig_files."""
    job_client = FakeJobClient()

    with tempfile.NamedTemporaryFile("w", delete=False) as file:
        temp_path = file.name

    try:
        conf.popen_cmake(
            job_client,
            project_dir,
            build_dir,
            kconfig_path=pathlib.Path(temp_path),
        )

        _, cmake_defs = parse_cmake_args(job_client.captured_argv)

        expected_kconfig_files = set(str(f) for f in conf.kconfig_files)
        expected_kconfig_files.add(temp_path)

        if expected_kconfig_files:
            kconfig_files = set(cmake_defs.pop("CONF_FILE").split(";"))
        else:
            assert "CONF_FILE" not in cmake_defs
            kconfig_files = set()

        assert cmake_defs == conf.cmake_defs
        assert kconfig_files == expected_kconfig_files

        kconfig_defs = util.read_kconfig_file(temp_path)
        assert kconfig_defs == conf.kconfig_defs
    finally:
        os.unlink(temp_path)


@pytest.fixture
def fake_kconfig_files(tmp_path):
    """Provide a list of 4 different fake kconfig file paths."""

    paths = [tmp_path / f"{letter}.conf" for letter in "ABCD"]

    for path, cfg_name in zip(paths, ("ONE", "TWO", "THREE", "FOUR")):
        path.write_text(
            f"# Fake kconfig file for testing.\nCONFIG_{cfg_name}=y\n"
        )

    return paths


def test_build_config_json_stability(fake_kconfig_files):
    """as_json() should return equivalent strings for two equivalent
    build configs.
    """
    config_a = BuildConfig(
        cmake_defs={
            "Z": "Y",
            "X": "W",
        },
        kconfig_defs={
            "CONFIG_A": "y",
            "CONFIG_B": "n",
        },
        kconfig_files=fake_kconfig_files,
    )

    # Dict ordering is intentionally reversed in b.
    config_b = BuildConfig(
        cmake_defs={
            "X": "W",
            "Z": "Y",
        },
        kconfig_defs={
            "CONFIG_B": "n",
            "CONFIG_A": "y",
        },
        kconfig_files=list(fake_kconfig_files),
    )

    assert config_a.as_json() == config_b.as_json()


def test_build_config_json_inequality():
    """Two differing build configs should not have the same json
    representation.
    """
    config_a = BuildConfig(cmake_defs={"A": "B"})
    config_b = BuildConfig(kconfig_defs={"CONFIG_A": "y"})

    assert config_a.as_json() != config_b.as_json()


def test_build_config_json_inequality_dtc_changes(tmp_path):
    """When DTC overlay files change, so should the JSON."""
    dts_file_1 = tmp_path / "overlay1.dts"
    dts_file_1.write_text("/* blah */\n")

    dts_file_2 = tmp_path / "overlay2.dts"
    dts_file_2.write_text("/* zonks! */\n")

    cfg = BuildConfig(
        cmake_defs={
            "DTC_OVERLAY_FILE": f"{dts_file_1};{dts_file_2}",
        },
    )

    orig_json = cfg.as_json()

    # Now, change dts_file_2!
    dts_file_2.write_text("/* I changed!! */\n")

    new_json = cfg.as_json()

    assert orig_json != new_json


def test_kconfig_file_duplicates(fake_kconfig_files):
    """Kconfig files should be like the "uniq" command.  Repeats should
    be removed, but not duplicates."""
    cfg = BuildConfig(
        kconfig_files=[
            fake_kconfig_files[0],
            fake_kconfig_files[0],
            fake_kconfig_files[1],
            fake_kconfig_files[0],
        ]
    )
    assert cfg.kconfig_files == [
        fake_kconfig_files[0],
        fake_kconfig_files[1],
        fake_kconfig_files[0],
    ]
