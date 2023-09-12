# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common settings and fixtures for all tests."""

import os
import pathlib

# pylint: disable=import-error
import hypothesis
import pytest
import zmake.zmake as zm


hypothesis.settings.register_profile(
    "cq", suppress_health_check=hypothesis.HealthCheck.all()
)
hypothesis.settings.load_profile("cq")

# pylint: disable=redefined-outer-name,unused-argument


@pytest.fixture
def zmake_factory_from_dir(tmp_path):
    """Creates module dirs and returns a Zmake object factory."""

    os.mkdir(tmp_path / "ec")
    os.mkdir(tmp_path / "ec" / "zephyr")
    (tmp_path / "ec" / "zephyr" / "module.yml").write_text("")
    zephyr_base = tmp_path / "zephyr_base"

    def _zmake_factory(**kwargs):
        return zm.Zmake(zephyr_base=zephyr_base, modules_dir=tmp_path, **kwargs)

    return _zmake_factory


@pytest.fixture
def zmake_from_dir(zmake_factory_from_dir):
    """Creates module dirs and returns a Zmake object."""
    return zmake_factory_from_dir()


@pytest.fixture
def fake_checkout(tmp_path):
    """Creates a fake checkout dir and returns the path to zmake."""

    actual_zmake_src_path = pathlib.Path(__file__).parent.parent
    fake_zmake_path = pathlib.Path(tmp_path) / "src/platform/ec/zephyr/zmake"
    os.makedirs(fake_zmake_path.parent)
    os.symlink(actual_zmake_src_path, fake_zmake_path)
    return fake_zmake_path


@pytest.fixture
def zmake_from_checkout(tmp_path, fake_checkout):
    """Creates a fake checkout dir and returns a Zmake object."""

    return zm.Zmake(checkout=tmp_path)
