# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Do a run of 'zmake build' and check the output"""

import logging
import os
import pathlib
import re
import unittest.mock

import pytest
from testfixtures import LogCapture

import zmake.build_config
import zmake.jobserver
import zmake.multiproc as multiproc
import zmake.output_packers
import zmake.project
import zmake.toolchains

OUR_PATH = os.path.dirname(os.path.realpath(__file__))


class FakeProject:
    """A fake project which requests two builds and does no packing"""

    # pylint: disable=too-few-public-methods,no-self-use

    def __init__(self):
        self.packer = unittest.mock.Mock()
        self.packer.pack_firmware = unittest.mock.Mock(return_value=[])

        self.config = zmake.project.ProjectConfig(
            project_name="fakeproject",
            zephyr_board="fakeboard",
            supported_toolchains=["llvm"],
            output_packer=zmake.output_packers.ElfPacker,
            project_dir=pathlib.Path("FakeProjectDir"),
        )

    @staticmethod
    def iter_builds():
        """Yield the two builds that zmake normally does"""
        yield "ro", zmake.build_config.BuildConfig()
        yield "rw", zmake.build_config.BuildConfig()

    def prune_modules(self, _):
        """Fake implementation of prune_modules."""
        return {}  # pathlib.Path('path')]

    def find_dts_overlays(self, _):
        """Fake implementation of find_dts_overlays."""
        return zmake.build_config.BuildConfig()

    def get_toolchain(self, module_paths, override=None):
        """Fake implementation of get_toolchain."""
        return zmake.toolchains.GenericToolchain(
            override or "foo",
            modules=module_paths,
        )


class FakeJobserver(zmake.jobserver.GNUMakeJobServer):
    """A fake jobserver which just runs 'cat' on the provided files"""

    def __init__(self, fnames):
        """Start up a jobserver with two jobs

        Args:
            fnames: Dict of regexp to filename. If the regexp matches the
            command, then the filename will be returned as the output.
        """
        super().__init__()
        self.jobserver = zmake.jobserver.GNUMakeJobServer(jobs=2)
        self.fnames = fnames

    def get_job(self):
        """Fake implementation of get_job(), which returns a real JobHandle()"""
        return zmake.jobserver.JobHandle(unittest.mock.Mock())

    # pylint: disable=arguments-differ
    def popen(self, cmd, *args, **kwargs):
        """Ignores the provided command and just runs 'cat' instead"""
        for pattern, filename in self.fnames.items():
            # Convert to a list of strings
            cmd = [isinstance(c, pathlib.PosixPath) and c.as_posix() or c for c in cmd]
            if pattern.match(" ".join(cmd)):
                new_cmd = ["cat", filename]
                break
        else:
            raise Exception('No pattern matched "%s"' % " ".join(cmd))
        kwargs.pop("env", None)
        return self.jobserver.popen(new_cmd, *args, **kwargs)


def get_test_filepath(suffix):
    """Get the filepath for a particular test file

    Args:
        suffix: Suffix of the file to read, e.g. 'ro' or 'ro_INFO'

    Returns:
        Full path to the test file
    """
    return os.path.join(OUR_PATH, "files", "sample_{}.txt".format(suffix))


def do_test_with_log_level(zmake_factory_from_dir, log_level, fnames=None):
    """Test filtering using a particular log level

    Args:
        log_level: Level to use
        use_configure: Run the 'configure' subcommand instead of 'build'
        fnames: Dict of regexp to filename. If the regexp matches the
            command, then the filename will be returned as the output.
            (None to use default ro/rw output)

    Returns:
        - List of log strings obtained from the run
    """
    if fnames is None:
        fnames = {
            re.compile(r".*build-ro"): get_test_filepath("ro"),
            re.compile(r".*build-rw"): get_test_filepath("rw"),
        }

    zmk = zmake_factory_from_dir(jobserver=FakeJobserver(fnames))
    with LogCapture(level=log_level) as cap:
        with unittest.mock.patch(
            "zmake.version.get_version_string", return_value="123"
        ), unittest.mock.patch.object(
            zmake.project,
            "find_projects",
            return_value={"fakeproject": FakeProject()},
        ), unittest.mock.patch(
            "zmake.version.write_version_header", autospec=True
        ):
            zmk.build(
                ["fakeproject"],
                clobber=True,
            )
        multiproc.wait_for_log_end()

    recs = [rec.getMessage() for rec in cap.records]
    return recs


class TestFilters:
    """Test filtering of stdout and stderr"""

    # pylint: disable=no-self-use

    def test_filter_normal(self, zmake_factory_from_dir):
        """Test filtering of a normal build (with no errors)"""
        recs = do_test_with_log_level(zmake_factory_from_dir, logging.ERROR)
        assert not recs

    def test_filter_info(self, zmake_factory_from_dir, tmp_path):
        """Test what appears on the INFO level"""
        recs = do_test_with_log_level(zmake_factory_from_dir, logging.INFO)
        # TODO: Remove sets and figure out how to check the lines are in the
        # right order.
        expected = {
            "Configuring fakeproject:rw.",
            "Configuring fakeproject:ro.",
            "Building fakeproject in {}/ec/build/zephyr/fakeproject.".format(tmp_path),
            "Building fakeproject:ro: /usr/bin/ninja -C {}-ro".format(
                tmp_path / "ec/build/zephyr/fakeproject/build"
            ),
            "Building fakeproject:rw: /usr/bin/ninja -C {}-rw".format(
                tmp_path / "ec/build/zephyr/fakeproject/build"
            ),
        }
        for suffix in ["ro", "rw"]:
            with open(get_test_filepath("%s_INFO" % suffix)) as file:
                for line in file:
                    expected.add("[fakeproject:{}]{}".format(suffix, line.strip()))
        # This produces an easy-to-read diff if there is a difference
        assert expected == set(recs)

    def test_filter_debug(self, zmake_factory_from_dir, tmp_path):
        """Test what appears on the DEBUG level"""
        recs = do_test_with_log_level(zmake_factory_from_dir, logging.DEBUG)
        # TODO: Remove sets and figure out how to check the lines are in the
        # right order.
        expected = {
            "Configuring fakeproject:rw.",
            "Configuring fakeproject:ro.",
            "Building fakeproject in {}/ec/build/zephyr/fakeproject.".format(tmp_path),
            "Building fakeproject:ro: /usr/bin/ninja -C {}-ro".format(
                tmp_path / "ec/build/zephyr/fakeproject/build"
            ),
            "Building fakeproject:rw: /usr/bin/ninja -C {}-rw".format(
                tmp_path / "ec/build/zephyr/fakeproject/build"
            ),
            "Running cat {}/files/sample_ro.txt".format(OUR_PATH),
            "Running cat {}/files/sample_rw.txt".format(OUR_PATH),
        }
        for suffix in ["ro", "rw"]:
            with open(get_test_filepath(suffix)) as file:
                for line in file:
                    expected.add("[fakeproject:{}]{}".format(suffix, line.strip()))
        # This produces an easy-to-read diff if there is a difference
        assert expected == set(recs)

    def test_filter_devicetree_error(self, zmake_factory_from_dir):
        """Test that devicetree errors appear"""
        recs = do_test_with_log_level(
            zmake_factory_from_dir,
            logging.ERROR,
            {re.compile(r".*"): get_test_filepath("err")},
        )

        dt_errs = [rec for rec in recs if "adc" in rec]
        assert "devicetree error: 'adc' is marked as required" in list(dt_errs)[0]


@pytest.mark.parametrize(
    ["project_names", "fmt", "search_dir", "expected_output"],
    [
        (
            ["link", "samus"],
            "{config.project_name}\n",
            None,
            "link\nsamus\n",
        ),
        (
            ["link", "samus"],
            "{config.project_name}\n",
            pathlib.Path("/foo/bar"),
            "link\nsamus\n",
        ),
        (
            [],
            "{config.project_name}\n",
            None,
            "",
        ),
        (
            ["link"],
            "",
            None,
            "",
        ),
        (
            ["link"],
            "{config.zephyr_board}\n",
            None,
            "some_board\n",
        ),
        (
            ["link"],
            "{config.project_name} is_test={config.is_test}\n",
            None,
            "link is_test=False\n",
        ),
    ],
)
def test_list_projects(
    project_names, fmt, search_dir, expected_output, capsys, zmake_from_dir
):  # pylint: disable=too-many-arguments
    """Test listing projects with default directory."""
    fake_projects = {
        name: zmake.project.Project(
            zmake.project.ProjectConfig(
                project_name=name,
                zephyr_board="some_board",
                supported_toolchains=["coreboot-sdk"],
                output_packer=zmake.output_packers.RawBinPacker,
            )
        )
        for name in project_names
    }
    with unittest.mock.patch(
        "zmake.project.find_projects",
        autospec=True,
        return_value=fake_projects,
    ):
        zmake_from_dir.list_projects(format=fmt, search_dir=search_dir)

    captured = capsys.readouterr()
    assert captured.out == expected_output
