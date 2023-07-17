# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for gen_bazel_targets.py."""

import gen_bazel_targets


def test_generated_file():
    """Test that the generated file is up-to-date."""
    assert (
        gen_bazel_targets.DEFAULT_OUTPUT.read_text(encoding="utf-8")
        == gen_bazel_targets.gen_bazel()
    ), "Build targets were changed.  Please run ./util/gen_bazel_targets.py."
