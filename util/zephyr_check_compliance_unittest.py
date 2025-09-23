#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for check_zephyr_project_config.py"""

import unittest


try:
    from unittest import mock
except ImportError:
    import mock  # pylint:disable=import-error

import zephyr_check_compliance


# pylint:disable=protected-access


class TestZephyrCheckCompliance(unittest.TestCase):
    """Tests for zephyr_check_compliance."""

    @mock.patch("check_compliance.get_files")
    def test_changed_files(self, get_files_mock):
        """Test _changed_files."""
        get_files_mock.return_value = [
            "file1",
            "file2",
        ]

        zephyr_check_compliance._patch_get_files()
        out = zephyr_check_compliance._changed_files("ref")
        self.assertFalse(out)

        get_files_mock.return_value.append("zephyr/file3")

        zephyr_check_compliance._patch_get_files()
        out = zephyr_check_compliance._changed_files("ref")
        self.assertTrue(out)

    @mock.patch("zephyr_check_compliance._changed_files")
    @mock.patch("check_compliance.main")
    def test_main(self, main_mock, changed_files_mock):
        """Tests the main function."""
        changed_files_mock.return_value = True

        zephyr_check_compliance.main(["ref"])

        changed_files_mock.assert_called_with("ref~1..ref")
        main_mock.assert_called_with(
            [
                "--output=",
                "--no-case-output",
                "-m",
                "YAMLLint",
                "-m",
                "DevicetreeBindings",
                "-c",
                "ref~1..ref",
            ]
        )

    @mock.patch("zephyr_check_compliance._changed_files")
    @mock.patch("check_compliance.main")
    def test_main_skip_presubmit(self, main_mock, changed_files_mock):
        """Tests the main function."""
        changed_files_mock.return_value = False

        zephyr_check_compliance.main([zephyr_check_compliance.PRE_SUBMIT_REF])

        self.assertEqual(changed_files_mock.call_count, 0)
        self.assertEqual(main_mock.call_count, 0)

    @mock.patch("zephyr_check_compliance._changed_files")
    @mock.patch("check_compliance.main")
    def test_main_skip(self, main_mock, changed_files_mock):
        """Tests the main function."""
        changed_files_mock.return_value = False

        zephyr_check_compliance.main(["ref"])

        changed_files_mock.assert_called_with("ref~1..ref")
        self.assertEqual(main_mock.call_count, 0)


if __name__ == "__main__":
    unittest.main()
