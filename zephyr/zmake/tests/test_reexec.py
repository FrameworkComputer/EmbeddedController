# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test the zmake re-exec functionality."""

import os
import sys
from unittest import mock

# pylint:disable=import-error
import pytest
import zmake.__main__ as main


@pytest.fixture(name="fake_env")
def fixture_fake_env(monkeypatch):
    """A test fixture that creates fake env variables."""
    environ = {}
    monkeypatch.setattr(os, "environ", environ)
    return environ


@pytest.fixture(name="mock_execve")
def fixture_mock_execve():
    """A test fixture that mocks the os.execve function."""
    with mock.patch("os.execve", autospec=True) as mocked_function:
        yield mocked_function


@pytest.mark.usefixtures("fake_env")
def test_out_of_chroot(mock_execve):
    """When CROS_WORKON_SRCROOT is not set, we should not re-exec."""
    main.maybe_reexec(["--help"])
    mock_execve.assert_not_called()


def test_pythonpath_set(fake_env, mock_execve):
    """With PYTHONPATH set, we should not re-exec."""
    fake_env["CROS_WORKON_SRCROOT"] = "/mnt/host/source"
    fake_env["PYTHONPATH"] = "/foo/bar/baz"
    main.maybe_reexec(["--help"])
    mock_execve.assert_not_called()


def test_zmake_does_not_exist(fake_env, mock_execve):
    """When zmake is not at src/platform/ec/zephyr/zmake, don't re-exec."""
    fake_env["CROS_WORKON_SRCROOT"] = "/this/does/not/exist"
    main.maybe_reexec(["--help"])
    mock_execve.assert_not_called()


def test_zmake_reexec(fake_env, mock_execve, tmp_path, fake_checkout):
    """Nothing else applies?  The re-exec should happen, when in a checkout."""
    fake_env["CROS_WORKON_SRCROOT"] = tmp_path
    main.maybe_reexec(["--help"])
    new_env = dict(fake_env)
    new_env["PYTHONPATH"] = str(fake_checkout.resolve())
    mock_execve.assert_called_once_with(
        sys.executable,
        [sys.executable, "-m", "zmake", "--help"],
        new_env,
    )
