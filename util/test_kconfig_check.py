# Copyright 2021 The Chromium OS Authors. All rights reserved.
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

    def check_read_configs(self, use_defines):
        checker = kconfig_check.KconfigCheck()
        with tempfile.NamedTemporaryFile() as configs:
            with open(configs.name, 'w') as out:
                prefix = '#define ' if use_defines else ''
                suffix = ' ' if use_defines else '='
                out.write(f'''{prefix}CONFIG_OLD_ONE{suffix}y
{prefix}NOT_A_CONFIG{suffix}
{prefix}CONFIG_STRING{suffix}"something"
{prefix}CONFIG_INT{suffix}123
{prefix}CONFIG_HEX{suffix}45ab
''')
            self.assertEqual(['OLD_ONE', 'STRING', 'INT', 'HEX'],
                             checker.read_configs(configs.name, use_defines))

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
        with open(os.path.join(srctree, 'Kconfig'), 'w') as out:
            out.write(f'''config {PREFIX}MY_KCONFIG
\tbool "my kconfig"

rsource "subdir/Kconfig.wibble"
''')
        subdir = os.path.join(srctree, 'subdir')
        os.mkdir(subdir)
        with open(os.path.join(subdir, 'Kconfig.wibble'), 'w') as out:
            out.write('menuconfig %sMENU_KCONFIG\n' % PREFIX)

        # Add a directory which should be ignored
        bad_subdir = os.path.join(subdir, 'Kconfig')
        os.mkdir(bad_subdir)
        with open(os.path.join(bad_subdir, 'Kconfig.bad'), 'w') as out:
            out.write('menuconfig %sBAD_KCONFIG' % PREFIX)

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
            self.assertEqual(['MENU_KCONFIG', 'MY_KCONFIG'],
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

    def test_real_kconfig(self):
        """Same Kconfig should be returned for kconfiglib / adhoc"""
        if not kconfig_check.USE_KCONFIGLIB:
            self.skipTest('No kconfiglib available')
        zephyr_path = pathlib.Path('../../third_party/zephyr/main').resolve()
        if not zephyr_path.exists():
            self.skipTest('No zephyr tree available')

        checker = kconfig_check.KconfigCheck()
        srcdir = 'zephyr'
        search_paths = [zephyr_path]
        kc_version = checker.scan_kconfigs(
            srcdir, search_paths=search_paths, try_kconfiglib=True)
        adhoc_version = checker.scan_kconfigs(srcdir, try_kconfiglib=False)

        # List of things missing from the Kconfig
        missing = sorted(list(set(adhoc_version) - set(kc_version)))

        # The Kconfig is disjoint in some places, e.g. the boards have their
        # own Kconfig files which are not included from the main Kconfig
        missing = [item for item in missing
                   if not item.startswith('BOARD') and
                   not item.startswith('VARIANT')]

        # Similarly, some other items are defined in files that are not included
        # in all cases, only for particular values of $(ARCH)
        self.assertEqual(
            ['FLASH_LOAD_OFFSET', 'NPCX_HEADER', 'SYS_CLOCK_HW_CYCLES_PER_SEC'],
            missing)

if __name__ == '__main__':
    unittest.main()
