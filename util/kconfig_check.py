#!/usr/bin/env python3
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Kconfig checker

Checks that the .config file provided does not introduce any new ad-hoc CONFIG
options
"""

import argparse
import os
import re
import sys


def parse_args(argv):
    """Parse the program arguments

    Args:
        argv: List of arguments to parse, excluding the program name

    Returns:
        argparse.Namespace object containing the results
    """
    epilog = """Checks that new ad-hoc CONFIG options are not introduced without
a corresponding Kconfig option for Zephyr"""

    parser = argparse.ArgumentParser(epilog=epilog)
    parser.add_argument('-a', '--allowed', type=str,
                        default='util/config_allowed.txt',
                        help='File containing list of allowed ad-hoc CONFIGs')
    parser.add_argument('-c', '--configs', type=str, default='.config',
                        help='File containing CONFIG options to check')
    parser.add_argument(
        '-D', '--debug', action='store_true',
        help='Enabling debugging (provides a full traceback on error)')
    parser.add_argument('-p', '--prefix', type=str, default='PLATFORM_EC_',
                        help='Prefix to string from Kconfig options')
    parser.add_argument('-s', '--srctree', type=str, default='.',
                        help='Path to source tree to look for Kconfigs')

    subparsers = parser.add_subparsers(dest='cmd', required=True)
    subparsers.add_parser('check', help='Check for new ad-hoc CONFIGs')

    return parser.parse_args(argv)


class KconfigCheck:
    """Class for handling checking of CONFIG options against Kconfig options

    The goal is to make sure that CONFIG_xxx options used by a build have an
    equivalent Kconfig option defined as well.

    For example if a Kconfig file has:

         config PREFIX_MY_CONFIG
             ...

    and the CONFIG files has

         CONFIG_MY_CONFIG

    then we consider these equivalent (with the prefix 'PREFIX_') and thus
    CONFIG_MY_CONFIG is allowed to be used.

    If any CONFIG option is found that does not have an equivalent in the Kconfig,
    the user is exhorted to add a new Kconfig. This helps avoid adding new ad-hoc
    CONFIG options, eventually returning the number to zero.
    """
    @classmethod
    def find_new_adhoc(cls, configs, kconfigs, allowed):
        """Get a list of new ad-hoc CONFIG options

        Arguments and return value should omit the 'CONFIG_' prefix, so
        CONFIG_LTO should be provided as 'LTO'.

        Args:
            configs: List of existing CONFIG options
            kconfigs: List of existing Kconfig options
            allowed: List of allowed CONFIG options

        Returns:
            List of new CONFIG options, with the CONFIG_ prefix removed
        """
        return sorted(list(set(configs) - set(kconfigs) - set(allowed)))

    @classmethod
    def read_configs(cls, configs_file):
        """Read CONFIG options from a file

        Args:
            configs_file: Filename to read from

        Returns:
            List of CONFIG_xxx options found in the file, with the 'CONFIG_'
                prefixremoved
        """
        with open(configs_file, 'r') as inf:
            configs = re.findall('CONFIG_([A-Za-z0-9_]*)', inf.read())
        return configs

    @classmethod
    def find_kconfigs(cls, srcdir):
        """Find all the Kconfig files in a source directory, recursively

        Any subdirectory called 'Kconfig' is ignored, since Zephyr generates
        this in its build directory.

        Args:
            srcdir: Directory to scan

        Returns:
            List of pathnames found
        """
        kconfig_files = []
        for root, dirs, files in os.walk(srcdir):
            kconfig_files += [os.path.join(root, fname)
                              for fname in files if fname.startswith('Kconfig')]
            if 'Kconfig' in dirs:
                dirs.remove('Kconfig')
        return kconfig_files

    def scan_kconfigs(self, srcdir, prefix=''):
        """Scan a source tree for Kconfig options

        Args:
            srcdir: Directory to scan
            prefix: Prefix to strip from the name (e.g. 'PLATFORM_EC_')

        Returns:
            List of config and menuconfig options found,
        """
        kconfigs = []

        # Remove the prefix if present
        expr = re.compile(r'(config|menuconfig) (%s)?([A-Za-z0-9_]*)\n' %
                          prefix)
        for fname in self.find_kconfigs(srcdir):
            with open(fname) as inf:
                found = re.findall(expr, inf.read())
                kconfigs += [name for kctype, _, name in found]
        return kconfigs

    def find_new_adhoc_configs(self, configs_file, srcdir, allowed_file,
                               prefix=''):
        """Find new ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            prefix: Prefix to strip from the start of each Kconfig
                (e.g. 'PLATFORM_EC_')
        """
        configs = self.read_configs(configs_file)
        kconfigs = self.scan_kconfigs(srcdir, prefix)
        allowed = self.read_configs(allowed_file)
        new_adhoc = self.find_new_adhoc(configs, kconfigs, allowed)
        return new_adhoc

    def do_check(self, configs_file, srcdir, allowed_file, prefix):
        """Find new ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            prefix: Prefix to strip from the start of each Kconfig
                (e.g. 'PLATFORM_EC_')

        Returns:
            Exit code: 0 if OK, 1 if a problem was found
        """
        new_adhoc = self.find_new_adhoc_configs(configs_file, srcdir,
                                                allowed_file, prefix)
        if new_adhoc:
            print("""Error:\tThe EC is in the process of migrating to Zephyr.
\tZephyr uses Kconfig for configuration rather than ad-hoc #defines.
\tAny new EC CONFIG options must ALSO be added to Zephyr so that new
\tfunctionality is available in Zephyr also. The following new ad-hoc
\tCONFIG options were detected:

%s

Please add these via Kconfig instead. Find a suitable Kconfig
file in zephyr/ and add a 'config' or 'menuconfig' option.
Also see details in http://issuetracker.google.com/181253613

To temporarily disable this, use: ALLOW_CONFIG=1 make ...
""" % '\n'.join(['CONFIG_%s' % name for name in new_adhoc]), file=sys.stderr)
            return 1
        return 0


def main(argv):
    """Main function"""
    args = parse_args(argv)
    if not args.debug:
        sys.tracebacklimit = 0
    checker = KconfigCheck()
    if args.cmd == 'check':
        return checker.do_check(args.configs, args.srctree, args.allowed,
                                args.prefix)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
