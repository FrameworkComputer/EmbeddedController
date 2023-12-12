# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Do a run of 'zmake build' and check the output"""

import logging
import os
import pathlib
import re
from typing import List, Union
import unittest.mock

# pylint: disable=import-error
import pytest
from testfixtures import LogCapture
from zmake import multiproc
import zmake.build_config
import zmake.jobserver
import zmake.output_packers
import zmake.project
import zmake.signers
import zmake.toolchains


OUR_PATH = os.path.dirname(os.path.realpath(__file__))


class FakeProject:
    """A fake project which requests two builds and does no packing"""

    def __init__(self):
        self.packer = unittest.mock.Mock()
        self.packer.pack_firmware = unittest.mock.Mock(return_value=[])

        self.config = zmake.project.ProjectConfig(
            project_name="fakeproject",
            zephyr_board="fakeboard",
            supported_toolchains=["llvm"],
            output_packer=zmake.output_packers.ElfPacker,
            project_dir=pathlib.Path("FakeProjectDir"),
            signer=zmake.signers.NullSigner(),
        )
        self.signer = self.config.signer

    @staticmethod
    def iter_builds():
        """Yield the two builds that zmake normally does"""
        yield "ro", zmake.build_config.BuildConfig()
        yield "rw", zmake.build_config.BuildConfig()

    @staticmethod
    def prune_modules(_):
        """Fake implementation of prune_modules."""
        return {}  # pathlib.Path('path')]

    @staticmethod
    def find_dts_overlays(_):
        """Fake implementation of find_dts_overlays."""
        return zmake.build_config.BuildConfig()

    @staticmethod
    def get_toolchain(module_paths, override=None):
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
        super().__init__(jobs=2)
        self.fnames = fnames

    def get_job(self):  # pylint: disable=no-self-use
        """Fake implementation of get_job(), which returns a real JobHandle()"""
        return zmake.jobserver.JobHandle(unittest.mock.Mock())

    def popen(self, argv: List[Union[str, pathlib.PosixPath]], **kwargs):
        """Ignores the provided command and just runs 'cat' instead"""
        # Convert to a list of strings
        cmd: List[str] = [
            isinstance(c, pathlib.PosixPath) and c.as_posix() or c for c in argv
        ]  # type: ignore
        for pattern, filename in self.fnames.items():
            if pattern.match(" ".join(cmd)):
                new_cmd = ["cat", filename]
                break
        else:
            raise Exception(f'No pattern matched "{" ".join(cmd)}"')
        kwargs["env"] = {}
        return super().popen(new_cmd, **kwargs)

    def env(self):  # pylint: disable=no-self-use
        """Runs test commands with an empty environment for simpler logs."""
        return {}


def get_test_filepath(suffix):
    """Get the filepath for a particular test file

    Args:
        suffix: Suffix of the file to read, e.g. 'ro' or 'ro_INFO'

    Returns:
        Full path to the test file
    """
    return os.path.join(OUR_PATH, "files", f"sample_{suffix}.txt")


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
        multiproc.LogWriter.wait_for_log_end()

    recs = [rec.getMessage() for rec in cap.records]
    return recs


class TestFilters:
    """Test filtering of stdout and stderr"""

    @staticmethod
    def test_filter_normal(zmake_factory_from_dir):
        """Test filtering of a normal build (with no errors)"""
        recs = do_test_with_log_level(zmake_factory_from_dir, logging.ERROR)
        assert not recs

    @staticmethod
    def test_filter_info(zmake_factory_from_dir, tmp_path, monkeypatch):
        """Test what appears on the INFO level"""
        monkeypatch.setattr(
            os, "environ", {"TOOL_PATH_ninja": "/usr/bin/ninja"}
        )
        recs = do_test_with_log_level(zmake_factory_from_dir, logging.INFO)
        # TODO: Remove sets and figure out how to check the lines are in the
        # right order.
        expected = {
            "Configuring fakeproject:rw.",
            "Configuring fakeproject:ro.",
            f"Building fakeproject in {tmp_path}/ec/build/zephyr/fakeproject.",
            "Building fakeproject:ro: /usr/bin/ninja -C "
            f"{tmp_path / 'ec/build/zephyr/fakeproject/build'}-ro",
            "Building fakeproject:rw: /usr/bin/ninja -C "
            f"{tmp_path / 'ec/build/zephyr/fakeproject/build'}-rw",
        }
        for suffix in ["ro", "rw"]:
            with open(
                get_test_filepath(f"{suffix}_INFO"), encoding="utf-8"
            ) as file:
                for line in file:
                    expected.add(f"[fakeproject:{suffix}]{line.strip()}")
        # This produces an easy-to-read diff if there is a difference
        assert expected == set(recs)

    @staticmethod
    def test_filter_debug(zmake_factory_from_dir, tmp_path, monkeypatch):
        """Test what appears on the DEBUG level"""
        monkeypatch.setattr(
            os, "environ", {"TOOL_PATH_ninja": "/usr/bin/ninja"}
        )
        recs = do_test_with_log_level(zmake_factory_from_dir, logging.DEBUG)
        # TODO: Remove sets and figure out how to check the lines are in the
        # right order.
        expected = {
            "Configuring fakeproject:rw.",
            "Configuring fakeproject:ro.",
            f"Building fakeproject in {tmp_path}/ec/build/zephyr/fakeproject.",
            "Building fakeproject:ro: /usr/bin/ninja -C "
            f"{tmp_path / 'ec/build/zephyr/fakeproject/build'}-ro",
            f"Building fakeproject:rw: /usr/bin/ninja -C "
            f"{tmp_path / 'ec/build/zephyr/fakeproject/build'}-rw",
            f"Running `env -i cat {OUR_PATH}/files/sample_ro.txt`",
            f"Running `env -i cat {OUR_PATH}/files/sample_rw.txt`",
        }
        for suffix in ["ro", "rw"]:
            with open(get_test_filepath(suffix), encoding="utf-8") as file:
                for line in file:
                    expected.add(f"[fakeproject:{suffix}]{line.strip()}")
        # This produces an easy-to-read diff if there is a difference
        assert expected == set(recs)

    @staticmethod
    def test_filter_devicetree_error(zmake_factory_from_dir):
        """Test that devicetree errors appear"""
        recs = do_test_with_log_level(
            zmake_factory_from_dir,
            logging.ERROR,
            {re.compile(r".*"): get_test_filepath("err")},
        )

        dt_errs = [rec for rec in recs if "adc" in rec]
        assert (
            "devicetree error: 'adc' is marked as required" in list(dt_errs)[0]
        )


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
            "{config.project_name} {config.zephyr_board}\n",
            None,
            "link some_board\n",
        ),
    ],
)
def test_list_projects(
    project_names,
    fmt,
    search_dir,
    expected_output,
    capsys,
    zmake_factory_from_dir,
):
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

    zmk = zmake_factory_from_dir(
        projects_dirs=[search_dir] if search_dir else None
    )
    with unittest.mock.patch(
        "zmake.project.find_projects",
        autospec=True,
        return_value=fake_projects,
    ):
        zmk.list_projects(fmt=fmt)

    captured = capsys.readouterr()
    assert captured.out == expected_output
