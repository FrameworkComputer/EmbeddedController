# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for gen_bazel_targets.py."""

import pathlib

import gen_bazel_targets
import pytest  # pylint: disable=import-error


def test_generated_file():
    """Test that the generated file is up-to-date."""
    # TODO(b/300287548): Generate the public and private bazel targets
    # separately.
    if not (pathlib.Path(__file__).parent.parent / "private").is_dir():
        pytest.skip("Don't test bazel generation in public checkouts")
    assert (
        gen_bazel_targets.DEFAULT_OUTPUT.read_text(encoding="utf-8")
        == gen_bazel_targets.gen_bazel()
    ), "Build targets were changed.  Please run ./bazel/gen_bazel_targets.py."
