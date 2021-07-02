# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import tempfile
import unittest.mock as mock

import hypothesis
import hypothesis.strategies as st
import pytest

import zmake.output_packers as packers

# Strategies for use with hypothesis
absolute_path = st.from_regex(regex=r"\A/[\w/]*\Z")

@hypothesis.given(absolute_path)
@hypothesis.settings(deadline=60000)
def test_file_size_unbounded(path):
    packer = packers.BasePacker(project=None)
    packer._is_size_bound = mock.Mock(name='_is_size_bound', return_value=False)
    file = pathlib.Path(path) / 'zephyr.bin'
    assert packer._check_packed_file_size(file=file, dirs={}) == file
    packer._is_size_bound.assert_called_once_with(file)

@hypothesis.given(st.binary(min_size=5, max_size=100))
@hypothesis.settings(deadline=60000)
def test_file_size_in_bounds(data):
    packer = packers.BasePacker(project=None)
    packer._is_size_bound = mock.Mock(name='_is_size_bound', return_value=True)
    packer._get_max_image_bytes = mock.Mock(name='_get_max_image_bytes',
                                            return_value=100)
    with tempfile.TemporaryDirectory() as temp_dir_name:
        file = pathlib.Path(temp_dir_name) / 'zephyr.bin'
        with open(file, 'wb') as f:
            f.write(data)
        assert packer._check_packed_file_size(file=file, dirs={}) == file

@hypothesis.given(st.binary(min_size=101, max_size=200))
@hypothesis.settings(deadline=60000)
def test_file_size_out_of_bounds(data):
    packer = packers.BasePacker(project=None)
    packer._is_size_bound = mock.Mock(name='_is_size_bound', return_value=True)
    packer._get_max_image_bytes = mock.Mock(name='_get_max_image_bytes',
                                            return_value=100)
    with tempfile.TemporaryDirectory() as temp_dir_name:
        file = pathlib.Path(temp_dir_name) / 'zephyr.bin'
        with open(file, 'wb') as f:
            f.write(data)
        with pytest.raises(RuntimeError):
            packer._check_packed_file_size(file=file, dirs={})