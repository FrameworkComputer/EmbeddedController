# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Test for Kconfig checker"""

import contextlib
import io
import os
import re
import sys
import tempfile
import unittest

import kconfig_check

# Prefix that we strip from each Kconfig option, when considering whether it is
# equivalent to a CONFIG option with the same name
PREFIX = 'PLATFORM_EC_'

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
        self.assertEqual(['NEW_ONE'], checker.find_new_adhoc(
            configs=['NEW_ONE', 'OLD_ONE', 'IN_KCONFIG'],
            kconfigs=['IN_KCONFIG'],
            allowed=['OLD_ONE']))

    def test_sorted_check(self):
        """Check it sorts the results in order"""
        checker = kconfig_check.KconfigCheck()
        self.assertSequenceEqual(
            ['ANOTHER_NEW_ONE', 'NEW_ONE'],
            checker.find_new_adhoc(
                configs=['NEW_ONE', 'ANOTHER_NEW_ONE', 'OLD_ONE', 'IN_KCONFIG'],
                kconfigs=['IN_KCONFIG'],
                allowed=['OLD_ONE']))

    def test_read_configs(self):
        """Test KconfigCheck.read_configs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.NamedTemporaryFile() as configs:
            with open(configs.name, 'w') as out:
                out.write("""CONFIG_OLD_ONE=y
NOT_A_CONFIG
CONFIG_STRING="something"
CONFIG_INT=123
CONFIG_HEX=45ab
""")
            self.assertEqual(['OLD_ONE', 'STRING', 'INT', 'HEX'],
                             checker.read_configs(configs.name))

    @classmethod
    def setup_srctree(cls, srctree):
        """Set up some Kconfig files in a directory and subdirs

        Args:
            srctree: Directory to write to
        """
        with open(os.path.join(srctree, 'Kconfig'), 'w') as out:
            out.write('config PLATFORM_EC_MY_KCONFIG\n')
        subdir = os.path.join(srctree, 'subdir')
        os.mkdir(subdir)
        with open(os.path.join(subdir, 'Kconfig.wibble'), 'w') as out:
            out.write('menuconfig PLATFORM_EC_MENU_KCONFIG\n')

        # Add a directory which should be ignored
        bad_subdir = os.path.join(subdir, 'Kconfig')
        os.mkdir(bad_subdir)
        with open(os.path.join(bad_subdir, 'Kconfig.bad'), 'w') as out:
            out.write('menuconfig PLATFORM_EC_BAD_KCONFIG')

    def test_find_kconfigs(self):
        """Test KconfigCheck.find_kconfigs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.TemporaryDirectory() as srctree:
            self.setup_srctree(srctree)
            files = checker.find_kconfigs(srctree)
            fnames = [fname[len(srctree):] for fname in files]
            self.assertEqual(['/Kconfig', '/subdir/Kconfig.wibble'], fnames)

    def test_scan_kconfigs(self):
        """Test KconfigCheck.scan_configs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.TemporaryDirectory() as srctree:
            self.setup_srctree(srctree)
            self.assertEqual(['MY_KCONFIG', 'MENU_KCONFIG'],
                             checker.scan_kconfigs(srctree, PREFIX))

    @classmethod
    def setup_allowed_and_configs(cls, allowed_fname, configs_fname):
        """Set up the 'allowed' and 'configs' files for tests

        Args:
            allowed_fname: Filename to write allowed CONFIGs to
            configs_fname: Filename to which CONFIGs to check should be written
        """
        with open(allowed_fname, 'w') as out:
            out.write('CONFIG_OLD_ONE')
        with open(configs_fname, 'w') as out:
            out.write('\n'.join(['CONFIG_OLD_ONE', 'CONFIG_NEW_ONE',
                                 'CONFIG_MY_KCONFIG']))

    def test_find_new_adhoc_configs(self):
        """Test KconfigCheck.find_new_adhoc_configs()"""
        checker = kconfig_check.KconfigCheck()
        with tempfile.TemporaryDirectory() as srctree:
            self.setup_srctree(srctree)
            with tempfile.NamedTemporaryFile() as allowed:
                with tempfile.NamedTemporaryFile() as configs:
                    self.setup_allowed_and_configs(allowed.name, configs.name)
                    result = checker.find_new_adhoc_configs(
                        configs.name, srctree, allowed.name, PREFIX)
                    self.assertEqual(['NEW_ONE'], result)

    def test_check(self):
        """Test running the 'check' subcommand"""
        with capture_sys_output() as (stdout, stderr):
            with tempfile.TemporaryDirectory() as srctree:
                self.setup_srctree(srctree)
                with tempfile.NamedTemporaryFile() as allowed:
                    with tempfile.NamedTemporaryFile() as configs:
                        self.setup_allowed_and_configs(allowed.name, configs.name)
                        ret_code = kconfig_check.main(
                            ['-c', configs.name, '-s', srctree,
                             '-a', allowed.name, '-p', PREFIX, 'check'])
                        self.assertEqual(1, ret_code)
        self.assertEqual('', stdout.getvalue())
        found = re.findall('(CONFIG_.*)', stderr.getvalue())
        self.assertEqual(['CONFIG_NEW_ONE'], found)


if __name__ == '__main__':
    unittest.main()
