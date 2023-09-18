#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for check_zephyr_project_config.py"""

# [VPYTHON:BEGIN]
# python_version: "3.8"
# wheel: <
#   name: "infra/python/wheels/mock-py3"
#   version: "version:4.0.3"
# >
# wheel: <
#   name: "infra/python/wheels/junitparser-py2_py3"
#   version: "version:2.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/future-py2_py3"
#   version: "version:0.18.2"
# >
# wheel: <
#   name: "infra/python/wheels/python-magic-py2_py3"
#   version: "version:0.4.24"
# >
# wheel: <
#   name: "infra/python/wheels/pyyaml-py3"
#   version: "version:5.3.1"
# >
# wheel: <
#   name: "infra/python/wheels/yamllint-py3"
#   version: "version:1.29.0"
# >
# wheel: <
#   name: "infra/python/wheels/pathspec-py3"
#   version: "version:0.9.0"
# >
# wheel: <
#   name: "infra/python/wheels/lxml/${vpython_platform}"
#   version: "version:4.6.3"
# >
# wheel: <
#   name: "infra/python/wheels/west-py3"
#   version: "version:1.1.0"
# >
# wheel: <
#   name: "infra/python/wheels/colorama-py2_py3"
#   version: "version:0.4.1"
# >
# wheel: <
#   name: "infra/python/wheels/packaging-py3"
#   version: "version:23.0"
# >
# wheel: <
#   name: "infra/python/wheels/pykwalify-py2_py3"
#   version: "version:1.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/ruamel_yaml-py3"
#   version: "version:0.17.16"
# >
# wheel: <
#   name: "infra/python/wheels/python-dateutil-py2_py3"
#   version: "version:2.8.1"
# >
# wheel: <
#   name: "infra/python/wheels/docopt-py2_py3"
#   version: "version:0.6.2"
# >
# wheel: <
#   name: "infra/python/wheels/six-py2_py3"
#   version: "version:1.16.0"
# >
# wheel: <
#   name: "infra/python/wheels/ruamel_yaml_clib/${vpython_platform}"
#   version: "version:0.2.6"
# >
# [VPYTHON:END]

import unittest


try:
    from unittest import mock
except ImportError:
    import mock  # pylint:disable=import-error

import zephyr_check_compliance


# pylint:disable=protected-access,no-self-use


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
