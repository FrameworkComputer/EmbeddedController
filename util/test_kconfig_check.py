# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test for Kconfig checker"""

import contextlib
import io
import os
import pathlib
import re
import sys
import tempfile
import unittest

import kconfig_check


# Prefixes that we strip from each Kconfig option, when considering whether it
# is equivalent to a CONFIG option with the same name.
PREFIX = "PLATFORM_EC_"
CONSOLE_PREFIX = "PLATFORM_EC_CONSOLE_CMD_"
PREFIX_TUPLES = [(PREFIX, ""), (CONSOLE_PREFIX, "CMD_")]
REPLACE_ARG_1 = PREFIX_TUPLES[0][0] + "," + PREFIX_TUPLES[0][1]
REPLACE_ARG_2 = PREFIX_TUPLES[1][0] + "," + PREFIX_TUPLES[1][1]


@contextlib.contextmanager
def capture_sys_output():
    """Capture output for testing purposes

    Use this to suppress stdout/stderr output:
        with capture_sys_output() as (stdout, stderr)
            ...do something...
    """
    capture_out, capture_err = io.StringIO(), io.StringIO()
    old_out, old_err = sys.stdout, sys.stderr
    try:
        sys.stdout, sys.stderr = capture_out, capture_err
        yield capture_out, capture_err
    finally:
        sys.stdout, sys.stderr = old_out, old_err


# Use unittest since it produced less verbose output than pytest and can be run
# directly from Python. You can still run this test with 'pytest' if you like.
class KconfigCheck(unittest.TestCase):
    """Tests for the KconfigCheck class"""

    def test_simple_check(self):
        """Check it detected a new ad-hoc CONFIG"""
        checker = kconfig_check.KconfigCheck()
        self.assertEqual(
            ["NEW_ONE"],
            checker.find_new_adhoc(
                configs=["NEW_ONE", "OLD_ONE", "IN_KCONFIG"],
                kconfigs=["IN_KCONFIG"],
                allowed=["OLD_ONE"],
            ),
        )

    def test_sorted_check(self):
        """Check it sorts the results in order"""
        checker = kconfig_check.KconfigCheck()
        self.assertSequenceEqual(
            ["ANOTHER_NEW_ONE", "NEW_ONE"],
            checker.find_new_adhoc(
                configs=["NEW_ONE", "ANOTHER_NEW_ONE", "OLD_ONE", "IN_KCONFIG"],
                kconfigs=["IN_KCONFIG"],
                allowed=["OLD_ONE"],
            ),
        )

    def check_read_configs(self, use_defines):
        """Check that kconfigs can be read."""
        checker = kconfig_check.KconfigCheck()
        with tempfile.NamedTemporaryFile() as configs:
            with open(configs.name, "w") as out:
                prefix = "#define " if use_defines else ""
                suffix = " " if use_defines else "="
                out.write(
                    f"""{prefix}CONFIG_OLD_ONE{suffix}y
{prefix}NOT_A_CONFIG{suffix}
{prefix}CONFIG_STRING{suffix}"something"
{prefix}CONFIG_INT{suffix}123
{prefix}CONFIG_HEX{suffix}45ab
"""
                )
            self.assertEqual(
                ["OLD_ONE", "STRING", "INT", "HEX"],
                checker.read_configs(configs.name, use_defines),
            )

    def test_read_configs(self):
        """Test KconfigCheck.read_configs()"""
        self.check_read_configs(False)

    def test_read_configs_defines(self):
        """Test KconfigCheck.read_configs() containing #defines"""
        self.check_read_configs(True)

    @classmethod
    def setup_srctree(cls, srctree):
        """Set up some Kconfig files in a directory and subdirs

        Args:
            srctree: Directory to write to
        """
        with open(os.path.join(srctree, "Kconfig"), "w") as out:
            out.write(
                f"""config {PREFIX}MY_KCONFIG
\tbool "my kconfig"

rsource "subdir/Kconfig.wibble"
"""
            )
        subdir = os.path.join(srctree, "subdir")
        os.mkdir(subdir)
        with open(os.path.join(subdir, "Kconfig.wibble"), "w") as out:
            out.write(
                f"""menuconfig {PREFIX}MENU_KCONFIG

config {CONSOLE_PREFIX}WIBBLE
\tbool "Console command: wibble"
"""
            )

        # Add a directory which should be ignored
        bad_subdir = os.path.join(subdir, "Kconfig")
        os.mkdir(bad_subdir)
        with open(os.path.join(bad_subdir, "Kconfig.bad"), "w") as out:
            out.write("menuconfig %sBAD_KCONFIG" % PREFIX)

    @classmethod
    def setup_zephyr_base(cls, zephyr_base):
        """Set up some Kconfig files in a directory and subdirs

        Args:
            zephyr_base: Directory to write to
        """
        with open(os.path.join(zephyr_base, "Kconfig.zephyr"), "w") as out:
            out.write(
                """config ZCONFIG
\tbool "zephyr kconfig"

rsource "subdir/Kconfig.wobble"
"""
            )
        subdir = os.path.join(zephyr_base, "subdir")
        os.mkdir(subdir)
        with open(os.path.join(subdir, "Kconfig.wobble"), "w") as out:
            out.write("menuconfig WOBBLE_MENU_KCONFIG\n")

        # Add a directory which should be ignored
        bad_subdir = os.path.join(subdir, "Kconfig")
        os.mkdir(bad_subdir)
        with open(os.path.join(bad_subdir, "Kconfig.bad"), "w") as out:
            out.write("menuconfig BAD_KCONFIG")

    def test_find_kconfigs(self):
        """Test KconfigCheck.find_kconfigs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.TemporaryDirectory() as srctree:
            self.setup_srctree(srctree)
            files = checker.find_kconfigs(srctree)
            fnames = [fname[len(srctree) :] for fname in files]
            self.assertEqual(["/Kconfig", "/subdir/Kconfig.wibble"], fnames)

    def test_scan_kconfigs(self):
        """Test KconfigCheck.scan_configs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.TemporaryDirectory() as srctree:
            with tempfile.TemporaryDirectory() as zephyr_path:
                self.setup_zephyr_base(zephyr_path)
                os.environ["ZEPHYR_BASE"] = str(zephyr_path)
                self.setup_srctree(srctree)
                self.assertEqual(
                    [
                        "CONSOLE_CMD_WIBBLE",
                        "MENU_KCONFIG",
                        "MY_KCONFIG",
                        "WOBBLE_MENU_KCONFIG",
                        "ZCONFIG",
                    ],
                    checker.scan_kconfigs(srctree, PREFIX_TUPLES),
                )

    @classmethod
    def setup_allowed_and_configs(
        cls, allowed_fname, configs_fname, add_new_one=True
    ):
        """Set up the 'allowed' and 'configs' files for tests

        Args:
            allowed_fname: Filename to write allowed CONFIGs to
            configs_fname: Filename to which CONFIGs to check should be written
            add_new_one: True to add CONFIG_NEW_ONE to the configs_fname file
        """
        with open(allowed_fname, "w") as out:
            out.write("CONFIG_CMD_WIBBLE\n")
            out.write("CONFIG_OLD_ONE\n")
            out.write("CONFIG_MENU_KCONFIG\n")
        with open(configs_fname, "w") as out:
            to_add = [
                "CONFIG_OLD_ONE",
                "CONFIG_MY_KCONFIG",
                "CONFIG_CMD_WIBBLE",
            ]
            if add_new_one:
                to_add.append("CONFIG_NEW_ONE")
            out.write("\n".join(to_add))

    def test_check_adhoc_configs(self):
        """Test KconfigCheck.check_adhoc_configs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.TemporaryDirectory() as srctree:
            self.setup_srctree(srctree)
            with tempfile.NamedTemporaryFile() as allowed:
                with tempfile.NamedTemporaryFile() as configs:
                    with tempfile.TemporaryDirectory() as zephyr_path:
                        self.setup_zephyr_base(zephyr_path)
                        os.environ["ZEPHYR_BASE"] = str(zephyr_path)
                        self.setup_allowed_and_configs(
                            allowed.name, configs.name
                        )
                        (
                            new_adhoc,
                            unneeded_adhoc,
                            updated_adhoc,
                        ) = checker.check_adhoc_configs(
                            configs.name, srctree, allowed.name, PREFIX_TUPLES
                        )
                        self.assertEqual(["NEW_ONE"], new_adhoc)
                        self.assertEqual(["MENU_KCONFIG"], unneeded_adhoc)
                        self.assertEqual(
                            ["CMD_WIBBLE", "OLD_ONE"], updated_adhoc
                        )

    def test_check(self):
        """Test running the 'check' subcommand"""
        with capture_sys_output() as (
            stdout,
            stderr,
        ), tempfile.TemporaryDirectory() as srctree:
            with tempfile.NamedTemporaryFile() as allowed:
                with tempfile.NamedTemporaryFile() as configs:
                    with tempfile.TemporaryDirectory() as zephyr_path:
                        self.setup_srctree(srctree)
                        self.setup_zephyr_base(zephyr_path)
                        os.environ["ZEPHYR_BASE"] = str(zephyr_path)
                        self.setup_allowed_and_configs(
                            allowed.name, configs.name
                        )
                        ret_code = kconfig_check.main(
                            [
                                "-c",
                                configs.name,
                                "-s",
                                srctree,
                                "-r",
                                REPLACE_ARG_1,
                                REPLACE_ARG_2,
                                "-a",
                                allowed.name,
                                "check",
                            ]
                        )
                        self.assertEqual(1, ret_code)
        self.assertEqual("", stdout.getvalue())
        found = re.findall("(CONFIG_.*)", stderr.getvalue())
        self.assertEqual(["CONFIG_NEW_ONE"], found)

    def test_real_kconfig(self):
        """Same Kconfig should be returned for kconfiglib / adhoc"""
        if not kconfig_check.USE_KCONFIGLIB:
            self.fail("No kconfiglib available")
        zephyr_path = pathlib.Path(
            "../../../src/third_party/zephyr/main"
        ).resolve()
        if not zephyr_path.exists():
            self.fail("No zephyr tree available")
        os.environ["ZEPHYR_BASE"] = str(zephyr_path)

        checker = kconfig_check.KconfigCheck()
        srcdir = "zephyr"
        search_paths = [zephyr_path]
        kc_version = checker.scan_kconfigs(
            srcdir, search_paths=search_paths, try_kconfiglib=True
        )
        adhoc_version = checker.scan_kconfigs(srcdir, try_kconfiglib=False)

        # List of things missing from the Kconfig
        missing = sorted(list(set(adhoc_version) - set(kc_version)))

        # There should be no differences between adhoc and kconfig versions
        self.assertListEqual([], missing)

    def test_check_unneeded(self):
        """Test running the 'check' subcommand with unneeded ad-hoc configs"""
        with capture_sys_output() as (stdout, stderr):
            with tempfile.TemporaryDirectory() as srctree:
                self.setup_srctree(srctree)
                with tempfile.NamedTemporaryFile() as allowed:
                    with tempfile.NamedTemporaryFile() as configs:
                        with tempfile.TemporaryDirectory() as zephyr_path:
                            self.setup_zephyr_base(zephyr_path)
                            os.environ["ZEPHYR_BASE"] = str(zephyr_path)
                            self.setup_allowed_and_configs(
                                allowed.name, configs.name, False
                            )
                            ret_code = kconfig_check.main(
                                [
                                    "-c",
                                    configs.name,
                                    "-s",
                                    srctree,
                                    "-r",
                                    REPLACE_ARG_1,
                                    REPLACE_ARG_2,
                                    "-a",
                                    allowed.name,
                                    "check",
                                ]
                            )
                            self.assertEqual(1, ret_code)
        self.assertEqual("", stderr.getvalue())
        found = re.findall("(CONFIG_.*)", stdout.getvalue())
        self.assertEqual(["CONFIG_CMD_WIBBLE", "CONFIG_MENU_KCONFIG"], found)
        allowed = kconfig_check.NEW_ALLOWED_FNAME.read_text().splitlines()
        self.assertEqual(["CONFIG_OLD_ONE"], allowed)


if __name__ == "__main__":
    unittest.main()
