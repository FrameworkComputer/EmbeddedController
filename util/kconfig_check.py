#!/usr/bin/env python3
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Kconfig checker

Checks that the .config file provided does not introduce any new ad-hoc CONFIG
options

This tool is also present in U-Boot, so we should keep the two in sync.

The tool supports two formats for the 'configs' file:

   CONFIG_SOMETHING=xx

and

   #define CONFIG_SOMETHING xx

Use the -d flag to select the second format.
"""

import argparse
import os
import pathlib
import re
import sys

# Try to use kconfiglib if available, but fall back to a simple recursive grep.
# This is used by U-Boot in some situations so we keep it to avoid forking this
# script.
USE_KCONFIGLIB = False
try:
    import kconfiglib
    USE_KCONFIGLIB = True
except ImportError:
    pass

# Where we put the new config_allowed file
NEW_ALLOWED_FNAME = pathlib.Path('/tmp/new_config_allowed.txt')


def parse_args(argv):
    """Parse the program arguments

    Args:
        argv: List of arguments to parse, excluding the program name

    Returns:
        argparse.Namespace object containing the results
    """
    epilog = '''Checks that new ad-hoc CONFIG options are not introduced without
a corresponding Kconfig option for Zephyr'''

    parser = argparse.ArgumentParser(epilog=epilog)
    parser.add_argument('-a', '--allowed', type=str,
                        default='util/config_allowed.txt',
                        help='File containing list of allowed ad-hoc CONFIGs')
    parser.add_argument('-c', '--configs', type=str, default='.config',
                        help='File containing CONFIG options to check')
    parser.add_argument('-d', '--use-defines', action='store_true',
                        help='Lines in the configs file use #define')
    parser.add_argument(
        '-D', '--debug', action='store_true',
        help='Enabling debugging (provides a full traceback on error)')
    parser.add_argument(
        '-i', '--ignore', action='append',
        help='Kconfig options to ignore (without CONFIG_ prefix)')
    parser.add_argument('-I', '--search-path', type=str, action='append',
                        help='Search paths to look for Kconfigs')
    parser.add_argument('-p', '--prefix', type=str, default='PLATFORM_EC_',
                        help='Prefix to string from Kconfig options')
    parser.add_argument('-s', '--srctree', type=str, default='zephyr/',
                        help='Path to source tree to look for Kconfigs')

    # TODO(sjg@chromium.org): The chroot uses a very old Python. Once it moves
    # to 3.7 or later we can use this instead:
    #    subparsers = parser.add_subparsers(dest='cmd', required=True)
    subparsers = parser.add_subparsers(dest='cmd')
    subparsers.required = True

    subparsers.add_parser('build', help='Build new list of ad-hoc CONFIGs')
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
        return sorted(set(configs) - set(kconfigs) - set(allowed))

    @classmethod
    def find_unneeded_adhoc(cls, kconfigs, allowed):
        """Get a list of ad-hoc CONFIG options that now have Kconfig options

        Arguments and return value should omit the 'CONFIG_' prefix, so
        CONFIG_LTO should be provided as 'LTO'.

        Args:
            kconfigs: List of existing Kconfig options
            allowed: List of allowed CONFIG options

        Returns:
            List of new CONFIG options, with the CONFIG_ prefix removed
        """
        return sorted(set(allowed) & set(kconfigs))

    @classmethod
    def get_updated_adhoc(cls, unneeded_adhoc, allowed):
        """Get a list of ad-hoc CONFIG options that are still needed

        Arguments and return value should omit the 'CONFIG_' prefix, so
        CONFIG_LTO should be provided as 'LTO'.

        Args:
            unneeded_adhoc: List of ad-hoc CONFIG options to remove
            allowed: Current list of allowed CONFIG options

        Returns:
            New version of allowed CONFIG options, with the CONFIG_ prefix
            removed
        """
        return sorted(set(allowed) - set(unneeded_adhoc))

    @classmethod
    def read_configs(cls, configs_file, use_defines=False):
        """Read CONFIG options from a file

        The file consists of a number of lines, each containing a CONFIG
        option

        Args:
            configs_file: Filename to read from (e.g. u-boot.cfg)
            use_defines: True if each line of the file starts with #define

        Returns:
            List of CONFIG_xxx options found in the file, with the 'CONFIG_'
                prefix removed
        """
        with open(configs_file, 'r') as inf:
            configs = re.findall('%sCONFIG_([A-Za-z0-9_]*)%s' %
                                 ((use_defines and '#define ' or ''),
                                  (use_defines and ' ' or '')),
                                 inf.read())
        return configs

    @classmethod
    def read_allowed(cls, allowed_file):
        """Read allowed CONFIG options from a file

        Args:
            allowed_file: Filename to read from

        Returns:
            List of CONFIG_xxx options found in the file, with the 'CONFIG_'
                prefix removed
        """
        with open(allowed_file, 'r') as inf:
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

    @classmethod
    def scan_kconfigs(cls, srcdir, prefix='', search_paths=None,
                      try_kconfiglib=True):
        """Scan a source tree for Kconfig options

        Args:
            srcdir: Directory to scan (containing top-level Kconfig file)
            prefix: Prefix to strip from the name (e.g. 'PLATFORM_EC_')
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory
            try_kconfiglib: Use kconfiglib if available

        Returns:
            List of config and menuconfig options found
        """
        if USE_KCONFIGLIB and try_kconfiglib:
            os.environ['srctree'] = srcdir
            kconf = kconfiglib.Kconfig('Kconfig', warn=False,
                                       search_paths=search_paths,
                                       allow_empty_macros=True)

            # There is always a MODULES config, since kconfiglib is designed for
            # linux, but we don't want it
            kconfigs = [name for name in kconf.syms if name != 'MODULES']

            if prefix:
                re_drop_prefix = re.compile(r'^%s' % prefix)
                kconfigs = [re_drop_prefix.sub('', name) for name in kconfigs]
        else:
            kconfigs = []
            # Remove the prefix if present
            expr = re.compile(r'\n(config|menuconfig) (%s)?([A-Za-z0-9_]*)\n' %
                              prefix)
            for fname in cls.find_kconfigs(srcdir):
                with open(fname) as inf:
                    found = re.findall(expr, inf.read())
                    kconfigs += [name for kctype, _, name in found]
        return sorted(kconfigs)

    def check_adhoc_configs(self, configs_file, srcdir, allowed_file,
                            prefix='', use_defines=False, search_paths=None):
        """Find new and unneeded ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            prefix: Prefix to strip from the start of each Kconfig
                (e.g. 'PLATFORM_EC_')
            use_defines: True if each line of the file starts with #define
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory

        Returns:
            Tuple:
                List of new ad-hoc CONFIG options (without 'CONFIG_' prefix)
                List of ad-hoc CONFIG options (without 'CONFIG_' prefix) that
                    are no-longer needed, since they now have an associated
                    Kconfig
                List of ad-hoc CONFIG options that are still needed, given the
                    current state of the Kconfig options
        """
        configs = self.read_configs(configs_file, use_defines)
        try:
            kconfigs = self.scan_kconfigs(srcdir, prefix, search_paths)
        except kconfiglib.KconfigError:
            # If we don't actually have access to the full Kconfig then we may
            # get an error. Fall back to using manual methods.
            kconfigs = self.scan_kconfigs(srcdir, prefix, search_paths,
                                          try_kconfiglib=False)

        allowed = self.read_allowed(allowed_file)
        new_adhoc = self.find_new_adhoc(configs, kconfigs, allowed)
        unneeded_adhoc = self.find_unneeded_adhoc(kconfigs, allowed)
        updated_adhoc = self.get_updated_adhoc(unneeded_adhoc, allowed)
        return new_adhoc, unneeded_adhoc, updated_adhoc

    def do_check(self, configs_file, srcdir, allowed_file, prefix, use_defines,
                 search_paths, ignore=None):
        """Find new ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            prefix: Prefix to strip from the start of each Kconfig
                (e.g. 'PLATFORM_EC_')
            use_defines: True if each line of the file starts with #define
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory
            ignore: List of Kconfig options to ignore if they match an ad-hoc
                CONFIG. This means they will not cause an error if they match
                an ad-hoc CONFIG.

        Returns:
            Exit code: 0 if OK, 1 if a problem was found
        """
        new_adhoc, unneeded_adhoc, updated_adhoc = self.check_adhoc_configs(
            configs_file, srcdir, allowed_file, prefix, use_defines,
            search_paths)
        if new_adhoc:
            file_list = '\n'.join(['CONFIG_%s' % name for name in new_adhoc])
            print(f'''Error:\tThe EC is in the process of migrating to Zephyr.
\tZephyr uses Kconfig for configuration rather than ad-hoc #defines.
\tAny new EC CONFIG options must ALSO be added to Zephyr so that new
\tfunctionality is available in Zephyr also. The following new ad-hoc
\tCONFIG options were detected:

{file_list}

Please add these via Kconfig instead. Find a suitable Kconfig
file in zephyr/ and add a 'config' or 'menuconfig' option.
Also see details in http://issuetracker.google.com/181253613

To temporarily disable this, use: ALLOW_CONFIG=1 make ...
''', file=sys.stderr)
            return 1

        if not ignore:
            ignore = []
        unneeded_adhoc = [name for name in unneeded_adhoc if name not in ignore]
        if unneeded_adhoc:
            with open(NEW_ALLOWED_FNAME, 'w') as out:
                for config in updated_adhoc:
                    print('CONFIG_%s' % config, file=out)
            now_in_kconfig = '\n'.join(
                ['CONFIG_%s' % name for name in unneeded_adhoc])
            print(f'''The following options are now in Kconfig:

{now_in_kconfig}

Please run this to update the list of allowed ad-hoc CONFIGs and include this
update in your CL:

   cp {NEW_ALLOWED_FNAME} util/config_allowed.txt
''')
            return 1
        return 0

    def do_build(self, configs_file, srcdir, allowed_file, prefix, use_defines,
                 search_paths):
        """Find new ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            prefix: Prefix to strip from the start of each Kconfig
                (e.g. 'PLATFORM_EC_')
            use_defines: True if each line of the file starts with #define
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory

        Returns:
            Exit code: 0 if OK, 1 if a problem was found
        """
        new_adhoc, _, updated_adhoc = self.check_adhoc_configs(
            configs_file, srcdir, allowed_file, prefix, use_defines,
            search_paths)
        with open(NEW_ALLOWED_FNAME, 'w') as out:
            combined = sorted(new_adhoc + updated_adhoc)
            for config in combined:
                print(f'CONFIG_{config}', file=out)
        print(f'New list is in {NEW_ALLOWED_FNAME}')

def main(argv):
    """Main function"""
    args = parse_args(argv)
    if not args.debug:
        sys.tracebacklimit = 0
    checker = KconfigCheck()
    if args.cmd == 'check':
        return checker.do_check(
            configs_file=args.configs, srcdir=args.srctree,
            allowed_file=args.allowed, prefix=args.prefix,
            use_defines=args.use_defines, search_paths=args.search_path,
            ignore=args.ignore)
    elif args.cmd == 'build':
        return checker.do_build(configs_file=args.configs, srcdir=args.srctree,
            allowed_file=args.allowed, prefix=args.prefix,
            use_defines=args.use_defines, search_paths=args.search_path)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
