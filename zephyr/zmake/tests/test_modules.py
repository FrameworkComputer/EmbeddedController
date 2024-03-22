# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test zmake modules module."""

import pathlib
import tempfile

# pylint:disable=import-error
import hypothesis
import hypothesis.strategies as st
import zmake.modules


module_lists = st.lists(
    st.one_of(*map(st.just, zmake.modules.known_modules)), unique=True
)


@hypothesis.given(module_lists)
@hypothesis.settings(deadline=None)
def test_locate_in_directory(modules):
    """Test the basic functionality of locate_from_directory"""

    with tempfile.TemporaryDirectory() as modules_dir:
        modules_dir = pathlib.Path(modules_dir).resolve()

        expected_modules = {}

        for module in modules:
            module_dir = modules_dir / module
            zephyr_dir = module_dir / "zephyr"
            zephyr_dir.mkdir(parents=True)

            module_yml = zephyr_dir / "module.yml"
            module_yml.write_bytes(b"")

            expected_modules[module] = module_dir

        assert (
            zmake.modules.locate_from_directory(modules_dir) == expected_modules
        )
