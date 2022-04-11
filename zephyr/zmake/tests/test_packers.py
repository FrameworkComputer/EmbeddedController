# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake packers."""

import pathlib
import tempfile

import hypothesis
import hypothesis.strategies as st
import pytest

import zmake.output_packers as packers

# Strategies for use with hypothesis
absolute_path = st.from_regex(regex=r"\A/[\w/]*\Z")


class FakePacker(packers.BasePacker):
    """Fake packer to expose protected methods."""

    def check_packed_file_size(self, file, dirs):
        """Expose the _check_packed_file_size method."""
        return self._check_packed_file_size(file, dirs)

    def _get_max_image_bytes(self):
        return 100

    def pack_firmware(self, work_dir, jobclient, version_string=""):
        assert False


@hypothesis.given(st.binary(min_size=101, max_size=200))
@hypothesis.settings(deadline=60000)
def test_file_size_unbounded(data):
    """Test with file size unbounded."""
    packer = FakePacker(project=None)
    with tempfile.TemporaryDirectory() as temp_dir_name:
        file = pathlib.Path(temp_dir_name) / "zephyr.elf"
        with open(file, "wb") as outfile:
            outfile.write(data)
        assert packer.check_packed_file_size(file=file, dirs={}) == file


@hypothesis.given(st.binary(min_size=5, max_size=100))
@hypothesis.settings(deadline=60000)
def test_file_size_in_bounds(data):
    """Test with file size limited."""
    packer = FakePacker(project=None)
    with tempfile.TemporaryDirectory() as temp_dir_name:
        file = pathlib.Path(temp_dir_name) / "zephyr.bin"
        with open(file, "wb") as outfile:
            outfile.write(data)
        assert packer.check_packed_file_size(file=file, dirs={}) == file


@hypothesis.given(st.binary(min_size=101, max_size=200))
@hypothesis.settings(deadline=60000)
def test_file_size_out_of_bounds(data):
    """Test with file size limited, and file exceeds limit."""
    packer = FakePacker(project=None)
    with tempfile.TemporaryDirectory() as temp_dir_name:
        file = pathlib.Path(temp_dir_name) / "zephyr.bin"
        with open(file, "wb") as outfile:
            outfile.write(data)
        with pytest.raises(RuntimeError):
            packer.check_packed_file_size(file=file, dirs={})
