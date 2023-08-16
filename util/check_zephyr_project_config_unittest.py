#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for check_zephyr_project_config.py"""

import os
import pathlib
import site
import unittest

import check_zephyr_project_config


try:
    from unittest import mock
except ImportError:
    import mock  # pylint:disable=import-error

site.addsitedir(check_zephyr_project_config.ZEPHYR_BASE / "scripts" / "kconfig")
# pylint:disable=import-error,wrong-import-position
import kconfiglib


# pylint:disable=protected-access


class TestKconfigCheck(unittest.TestCase):
    """Tests for KconfigCheck."""

    @mock.patch("check_zephyr_project_config.KconfigCheck._init_kconfig")
    def setUp(self, init_kconfig_mock):  # pylint:disable=arguments-differ
        mock_kconfig = mock.Mock()
        init_kconfig_mock.return_value = mock_kconfig

        self.kcheck = check_zephyr_project_config.KconfigCheck(True)

        self.assertEqual(self.kcheck.fail_count, 0)
        self.assertEqual(self.kcheck.program_kconf, {None: mock_kconfig})

    @mock.patch("zephyr_module.parse_modules")
    @mock.patch("zephyr_module.process_kconfig")
    @mock.patch("kconfiglib.Kconfig")
    def test_init_kconfig(
        self, kconfig_mock, process_kconfig_mock, parse_modules_mock
    ):
        """Initialize a Kconfig with the upstream Kconfig file."""
        mock_module = mock.Mock()
        mock_module.project = "project"
        mock_module.meta = "meta"
        parse_modules_mock.return_value = [mock_module]
        process_kconfig_mock.return_value = "fake kconfig"

        os.environ["ZEPHYR_BASE"] = "/totally/not/zephyr/base"
        os.environ["srctree"] = "/also/not/zephyr/base"
        os.environ["ARCH_DIR"] = "not_an_arch_dir"
        os.environ["ARCH"] = "not_a_star"
        os.environ["BOARD_DIR"] = "something_else"

        self.kcheck._init_kconfig(None)

        parse_modules_mock.assert_called_once_with(
            check_zephyr_project_config.ZEPHYR_BASE,
            modules=[check_zephyr_project_config.EC_BASE],
        )
        process_kconfig_mock.assert_called_once_with("project", "meta")
        kconfig_mock.assert_called_once_with(
            str(check_zephyr_project_config.ZEPHYR_BASE / "Kconfig")
        )

        self.assertEqual(
            os.environ["ZEPHYR_BASE"],
            str(check_zephyr_project_config.ZEPHYR_BASE),
        )
        self.assertEqual(
            os.environ["srctree"], str(check_zephyr_project_config.ZEPHYR_BASE)
        )
        self.assertEqual(os.environ["ARCH_DIR"], "arch")
        self.assertEqual(os.environ["ARCH"], "*")
        self.assertEqual(os.environ["BOARD_DIR"], "boards/posix/native_posix")

    @mock.patch("zephyr_module.parse_modules")
    @mock.patch("zephyr_module.process_kconfig")
    @mock.patch("kconfiglib.Kconfig")
    def test_init_kconfig_filename(
        self, kconfig_mock, process_kconfig_mock, parse_modules_mock
    ):
        """Initialize a Kconfig with a specific path."""
        kconfig_path = "my/project/Kconfig"

        self.kcheck._init_kconfig(kconfig_path)

        kconfig_mock.assert_called_once_with(kconfig_path)
        self.assertEqual(process_kconfig_mock.call_count, 0)
        parse_modules_mock.assert_called_once_with(mock.ANY, modules=mock.ANY)

    @mock.patch("pathlib.Path.is_file")
    @mock.patch("check_zephyr_project_config.KconfigCheck._init_kconfig")
    def test_kconf_from_path(self, kconfig_mock, is_file_mock):
        """Test the cached Kconfig load from path."""
        fake_kconfig_upstream = mock.Mock()
        fake_kconfig_program = mock.Mock()
        fake_kconfig_program_new = mock.Mock()
        kconfig_mock.return_value = fake_kconfig_program_new
        self.kcheck.program_kconf = {
            None: fake_kconfig_upstream,
            "cached_program": fake_kconfig_program,
        }

        # random project
        out = self.kcheck._kconf_from_path("random/path")

        self.assertEqual(out, fake_kconfig_upstream)
        self.assertEqual(kconfig_mock.call_count, 0)
        self.assertEqual(is_file_mock.call_count, 0)

        # already loaded
        is_file_mock.return_value = True
        fake_path = pathlib.Path(
            check_zephyr_project_config.EC_BASE,
            "zephyr",
            "program",
            "cached_program",
        )

        out = self.kcheck._kconf_from_path(fake_path)

        self.assertEqual(out, fake_kconfig_program)
        self.assertEqual(kconfig_mock.call_count, 0)
        is_file_mock.assert_called_once_with()
        is_file_mock.reset_mock()

        # project with no kconfig
        is_file_mock.return_value = False
        fake_path = pathlib.Path(
            check_zephyr_project_config.EC_BASE,
            "zephyr",
            "program",
            "program_with_no_kconfig",
        )

        out = self.kcheck._kconf_from_path(fake_path)

        self.assertEqual(out, fake_kconfig_upstream)
        self.assertEqual(kconfig_mock.call_count, 0)
        is_file_mock.assert_called_once_with()
        is_file_mock.reset_mock()

        # project with kconfig
        is_file_mock.return_value = True
        fake_path = pathlib.Path(
            check_zephyr_project_config.EC_BASE,
            "zephyr",
            "program",
            "program_with_kconfig",
        )

        out = self.kcheck._kconf_from_path(fake_path)

        self.assertEqual(out, fake_kconfig_program_new)
        kconfig_mock.assert_called_once_with(pathlib.Path(fake_path, "Kconfig"))
        is_file_mock.assert_called_once_with()
        is_file_mock.reset_mock()

        # final cache state
        self.assertEqual(
            self.kcheck.program_kconf,
            {
                None: fake_kconfig_upstream,
                "cached_program": fake_kconfig_program,
                "program_with_kconfig": fake_kconfig_program_new,
                "program_with_no_kconfig": fake_kconfig_upstream,
            },
        )

    def test_fail(self):
        """Test the fail method."""
        with mock.patch.object(self.kcheck, "log") as log_mock:
            self.assertEqual(self.kcheck.fail_count, 0)

            self.kcheck._fail("broken")

            self.assertEqual(self.kcheck.fail_count, 1)
            log_mock.error.assert_called_once_with("broken")

    def test_filter_config_files(self):
        """Test the config file filter."""
        file_no_exist = mock.Mock()
        file_no_exist.exists.return_value = False

        file_exists = mock.Mock()
        file_exists.exists.return_value = True
        file_exists.name = "definitely_not_a_conf"

        file_conf = mock.Mock()
        file_conf.exists.return_value = True
        file_conf.name = "blah.conf"

        files = [file_no_exist, file_exists, file_conf]

        out = list(self.kcheck._filter_config_files(files))

        self.assertEqual(out, [file_conf])

    @mock.patch("check_zephyr_project_config._default_y_if_ztest")
    @mock.patch("check_zephyr_project_config._default_y")
    @mock.patch("kconfiglib.expr_str")
    @mock.patch("check_zephyr_project_config.KconfigCheck._kconf_from_path")
    @mock.patch("check_zephyr_project_config.KconfigCheck._fail")
    def test_check_dt_has(
        self,
        fail_mock,
        kconf_from_path_mock,
        expr_str_mock,
        def_y_mock,
        def_y_if_ztest_mock,
    ):
        """Test the DT_HAS_ check."""
        fake_symbol_no_dt = mock.Mock()
        fake_symbol_no_dt.direct_dep = "nothing with devicetree"

        fake_symbol_dt = mock.Mock()
        fake_symbol_dt.direct_dep = (
            "a bunch of stuff with DT_HAS_something in the middle"
        )

        fake_symbol_dt_default_n = mock.Mock()
        fake_symbol_dt_default_n.direct_dep = (
            "a bunch of stuff with DT_HAS_something in the middle"
        )
        fake_symbol_dt_default_n.orig_defaults = mock.Mock()

        fake_symbol_dt_emul = mock.Mock()
        fake_symbol_dt_emul.direct_dep = (
            "a bunch of stuff with DT_HAS_something in the middle"
        )
        fake_symbol_dt_emul.orig_defaults = mock.Mock()

        fake_kconf = mock.Mock()
        fake_kconf.syms = {
            "NO_DT": fake_symbol_no_dt,
            "DT": fake_symbol_dt,
            "DT_BUT_DEFAULT_N": fake_symbol_dt_default_n,
            "DT_BUT_EMUL": fake_symbol_dt_emul,
        }

        def_y_mock.side_effect = (
            lambda deps: deps != fake_symbol_dt_default_n.orig_defaults
        )
        def_y_if_ztest_mock.side_effect = (
            lambda deps: deps == fake_symbol_dt_emul.orig_defaults
        )

        kconf_from_path_mock.return_value = fake_kconf
        expr_str_mock.side_effect = lambda val: val

        data = """
# some comment
CONFIG_NOT_RELATED=n
CONFIG_NO_DT=y
CONFIG_NO_DT=n
CONFIG_DT=y
CONFIG_DT=y # and a comment
CONFIG_DT=n
CONFIG_SOMETHING_ELSE=y
CONFIG_DT_BUT_DEFAULT_N=y
CONFIG_DT_BUT_EMUL=y
"""
        with mock.patch("builtins.open", mock.mock_open(read_data=data)):
            self.kcheck._check_dt_has("the_file")

        self.assertListEqual(
            fail_mock.call_args_list,
            [
                mock.call(
                    mock.ANY,
                    "the_file",
                    6,
                    "CONFIG_DT=y",
                    fake_symbol_dt.direct_dep,
                ),
                mock.call(
                    mock.ANY,
                    "the_file",
                    7,
                    "CONFIG_DT=y",
                    fake_symbol_dt.direct_dep,
                ),
            ],
        )

    @mock.patch("check_zephyr_project_config.KconfigCheck._check_dt_has")
    @mock.patch("check_zephyr_project_config.KconfigCheck._filter_config_files")
    def test_run_checks_dt_has(
        self, filter_config_files_mock, check_dt_has_mock
    ):
        """Test the run_check method for dt_has."""
        filter_config_files_mock.return_value = ["a", "b"]

        self.kcheck.run_checks([], True)

        self.assertListEqual(
            check_dt_has_mock.call_args_list, [mock.call("a"), mock.call("b")]
        )


class TestKconfigSymbolChecks(unittest.TestCase):
    """Tests for symbol checks."""

    def setUp(self):  # pylint:disable=arguments-differ
        self.sym_y = kconfiglib.Symbol()
        self.sym_y.name = "y"
        self.sym_y.is_constant = True

        self.sym_n = kconfiglib.Symbol()
        self.sym_n.name = "n"
        self.sym_n.is_constant = True

        self.sym_ztest = kconfiglib.Symbol()
        self.sym_ztest.name = "ZTEST"

        self.sym_not_ztest = (kconfiglib.NOT, self.sym_ztest)

    def test_default_y(self):
        """Test the _default_y function."""
        self.assertTrue(
            check_zephyr_project_config._default_y([[self.sym_y, self.sym_y]])
        )
        self.assertTrue(
            check_zephyr_project_config._default_y(
                [[self.sym_y, self.sym_not_ztest]]
            )
        )
        self.assertTrue(
            check_zephyr_project_config._default_y(
                [[self.sym_y, self.sym_ztest]]
            )
        )
        self.assertFalse(
            check_zephyr_project_config._default_y([[self.sym_n, self.sym_n]])
        )

    def test_default_y_if_ztest(self):
        """Test the _default_y_if_ztest function."""
        self.assertFalse(
            check_zephyr_project_config._default_y_if_ztest(
                [[self.sym_y, self.sym_y]]
            )
        )
        self.assertFalse(
            check_zephyr_project_config._default_y_if_ztest(
                [[self.sym_y, self.sym_not_ztest]]
            )
        )
        self.assertTrue(
            check_zephyr_project_config._default_y_if_ztest(
                [[self.sym_y, self.sym_ztest]]
            )
        )
        self.assertFalse(
            check_zephyr_project_config._default_y_if_ztest(
                [[self.sym_n, self.sym_n]]
            )
        )


if __name__ == "__main__":
    unittest.main()
