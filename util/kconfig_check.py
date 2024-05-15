#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
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
import glob
import os
import pathlib
import re
import sys
import tempfile
import traceback


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
NEW_ALLOWED_FNAME = pathlib.Path("/tmp/new_config_allowed.txt")


def prefix_adhoc_tuple(arg_string):
    """Replace argument type

    Args:
        arg_string: Command line argument string containing comma separated
        prefix/adhoc replacement pattern.

    Returns:
        Tuple of the Kconfig prefix following the adhoc replacement string.
    """
    try:
        prefix_tuple = tuple(arg_string.split(","))
    except Exception as err:
        raise argparse.ArgumentTypeError(
            "Replace argument must 'prefix,replace`"
        ) from err

    if len(prefix_tuple) != 2:
        raise argparse.ArgumentTypeError(
            "Replace argument must 'prefix,replace`"
        )

    return prefix_tuple


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
    parser.add_argument(
        "-a",
        "--allowed",
        type=str,
        default="util/config_allowed.txt",
        help="File containing list of allowed ad-hoc CONFIGs",
    )
    parser.add_argument(
        "-c",
        "--configs",
        type=str,
        default=".config",
        help="File containing CONFIG options to check",
    )
    parser.add_argument(
        "-d",
        "--use-defines",
        action="store_true",
        help="Lines in the configs file use #define",
    )
    parser.add_argument(
        "-D",
        "--debug",
        action="store_true",
        help="Enabling debugging (provides a full traceback on error)",
    )
    parser.add_argument(
        "-i",
        "--ignore",
        action="append",
        help="Kconfig options to ignore (without CONFIG_ prefix)",
    )
    parser.add_argument(
        "-I",
        "--search-path",
        type=str,
        action="append",
        help="Search paths to look for Kconfigs",
    )
    parser.add_argument(
        "-r",
        "--replace",
        metavar="prefix,adhoc",
        dest="replace_list",
        default=[("PLATFORM_EC_", "")],
        type=prefix_adhoc_tuple,
        nargs="+",
        help="Replace a prefix string from Kconfig with an ad-hoc string",
    )
    parser.add_argument(
        "-s",
        "--srctree",
        type=str,
        default="zephyr/",
        help="Path to source tree to look for Kconfigs",
    )

    # TODO(sjg@chromium.org): The chroot uses a very old Python. Once it moves
    # to 3.7 or later we can use this instead:
    #    subparsers = parser.add_subparsers(dest='cmd', required=True)
    subparsers = parser.add_subparsers(dest="cmd")
    subparsers.required = True

    subparsers.add_parser("build", help="Build new list of ad-hoc CONFIGs")
    subparsers.add_parser("check", help="Check for new ad-hoc CONFIGs")
    subparsers.add_parser(
        "check_undef", help="Verify #undef directives in ec headers"
    )

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
        with open(configs_file, "r", encoding="utf-8") as inf:
            configs = re.findall(
                f'{use_defines and "#define " or ""}CONFIG_([A-Za-z0-9_]*)'
                f'{use_defines and " " or ""}',
                inf.read(),
            )
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
        with open(allowed_file, "r", encoding="utf-8") as inf:
            configs = re.findall("CONFIG_([A-Za-z0-9_]*)", inf.read())
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
            kconfig_files += [
                os.path.join(root, fname)
                for fname in files
                if fname.startswith("Kconfig")
            ]
            if "Kconfig" in dirs:
                dirs.remove("Kconfig")
            if "boards" in dirs:
                dirs.remove("boards")
            if "program" in dirs:
                dirs.remove("program")
            if "test" in dirs:
                dirs.remove("test")
            if "chip" in dirs:
                dirs.remove("chip")
        return kconfig_files

    @classmethod
    def fixup_symbols(cls, symbols, replace_list):
        """Fixup all Kconfig symbols using the prefix/adhoc replacement rules"""
        if replace_list is not None:
            for replace in replace_list:
                # Replace tuples are a Kconfig prefix and the adhoc
                # substitution.
                re_drop_prefix = re.compile(f"^{replace[0]}")
                symbols = [
                    re_drop_prefix.sub(repl=replace[1], string=name)
                    for name in symbols
                ]
        return symbols

    @classmethod
    def scan_kconfigs(
        cls,
        srcdir,
        replace_list=None,
        search_paths=None,
        try_kconfiglib=True,
    ):
        """Scan a source tree for Kconfig options

        Args:
            srcdir: Directory to scan (containing top-level Kconfig file)
            replace_list: List of prefix/adhoc tuples.  The "prefix" is removed
                from Kconfig symbols and replaced by "adhoc".
                e.g. ('PLATFORM_EC, '')
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory
            try_kconfiglib: Use kconfiglib if available

        Returns:
            List of config and menuconfig options found
        """
        if USE_KCONFIGLIB and try_kconfiglib:
            with tempfile.TemporaryDirectory() as temp_dir:
                (pathlib.Path(temp_dir) / "Kconfig.modules").touch()
                (pathlib.Path(temp_dir) / "soc").mkdir()
                (pathlib.Path(temp_dir) / "soc" / "Kconfig.defconfig").touch()
                (pathlib.Path(temp_dir) / "soc" / "Kconfig.soc").touch()
                (pathlib.Path(temp_dir) / "arch").mkdir()
                (pathlib.Path(temp_dir) / "arch" / "Kconfig").touch()

                os.environ.update(
                    {
                        "srctree": srcdir,
                        "SOC_DIR": "soc",
                        "ARCH_DIR": "arch",
                        "BOARD_DIR": "boards/*/*",
                        "ARCH": "*",
                        "KCONFIG_BINARY_DIR": temp_dir,
                        "HWM_SCHEME": "v2",
                    }
                )
                kconfigs = []
                for filename in [
                    "Kconfig",
                    os.path.join(os.environ["ZEPHYR_BASE"], "Kconfig.zephyr"),
                ]:
                    kconf = kconfiglib.Kconfig(
                        filename,
                        warn=False,
                        search_paths=search_paths,
                        allow_empty_macros=True,
                    )

                    symbols = [
                        node.item.name
                        for node in kconf.node_iter()
                        if isinstance(node.item, kconfiglib.Symbol)
                    ]

                    symbols = cls.fixup_symbols(symbols, replace_list)

                    kconfigs += symbols
        else:
            symbols = []
            kconfigs = []
            # Remove the prefix if present
            expr = re.compile(r"\n(config|menuconfig) ([A-Za-z0-9_]*)\n")
            for fname in cls.find_kconfigs(srcdir):
                with open(fname, encoding="utf-8") as inf:
                    found = re.findall(expr, inf.read())
                    symbols += [name for kctype, name in found]

            symbols = cls.fixup_symbols(symbols, replace_list)
            kconfigs += symbols
        return sorted(kconfigs)

    def check_adhoc_configs(
        self,
        configs_file,
        srcdir,
        allowed_file,
        replace_list=None,
        use_defines=False,
        search_paths=None,
    ):
        """Find new and unneeded ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            replace_list: List of prefix/adhoc tuples.  The "prefix" is removed
                from Kconfig symbols and replaced by "adhoc".
                e.g. ('PLATFORM_EC, '')
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
            kconfigs = self.scan_kconfigs(srcdir, replace_list, search_paths)
        except kconfiglib.KconfigError:
            # If we don't actually have access to the full Kconfig then we may
            # get an error. Fall back to using manual methods.
            print("WARNING: kconfiglib failed", file=sys.stderr)
            traceback.print_exc()
            kconfigs = self.scan_kconfigs(
                srcdir,
                replace_list,
                search_paths,
                try_kconfiglib=False,
            )

        allowed = self.read_allowed(allowed_file)
        new_adhoc = self.find_new_adhoc(configs, kconfigs, allowed)
        unneeded_adhoc = self.find_unneeded_adhoc(kconfigs, allowed)
        updated_adhoc = self.get_updated_adhoc(unneeded_adhoc, allowed)
        return new_adhoc, unneeded_adhoc, updated_adhoc

    def do_check(
        self,
        configs_file,
        srcdir,
        allowed_file,
        replace_list,
        use_defines,
        search_paths,
        ignore=None,
    ):
        """Find new ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            replace_list: List of prefix,adhoc tuples.  The prefix is stripped
                from each Kconfig and replaced with the adhoc string prior to
                comparison.  (e.e. ['PLATFORM_EC',''])
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
            configs_file,
            srcdir,
            allowed_file,
            replace_list,
            use_defines,
            search_paths,
        )
        if new_adhoc:
            file_list = "\n".join([f"CONFIG_{name}" for name in new_adhoc])
            print(
                f"""Error:\tThe EC is in the process of migrating to Zephyr.
\tZephyr uses Kconfig for configuration rather than ad-hoc #defines.
\tAny new EC CONFIG options must ALSO be added to Zephyr so that new
\tfunctionality is available in Zephyr also. The following new ad-hoc
\tCONFIG options were detected:

{file_list}

Please add these via Kconfig instead. Find a suitable Kconfig
file in zephyr/ and add a 'config' or 'menuconfig' option.
Also see details in http://issuetracker.google.com/181253613

To temporarily disable this, use: ALLOW_CONFIG=1 make ...
""",
                file=sys.stderr,
            )
            return 1

        if not ignore:
            ignore = []
        unneeded_adhoc = [name for name in unneeded_adhoc if name not in ignore]
        if unneeded_adhoc:
            with open(NEW_ALLOWED_FNAME, "w", encoding="utf-8") as out:
                for config in updated_adhoc:
                    print(f"CONFIG_{config}", file=out)
            now_in_kconfig = "\n".join(
                [f"CONFIG_{name}" for name in unneeded_adhoc]
            )
            print(
                f"""The following options are now in Kconfig:

{now_in_kconfig}

Please run this to update the list of allowed ad-hoc CONFIGs and include this
update in your CL:

   cp {NEW_ALLOWED_FNAME} util/config_allowed.txt
"""
            )
            return 1
        return 0

    def do_build(
        self,
        configs_file,
        srcdir,
        allowed_file,
        replace_list,
        use_defines,
        search_paths,
    ):
        """Find new ad-hoc configs in the configs_file

        Args:
            configs_file: Filename containing CONFIG options to check
            srcdir: Source directory to scan for Kconfig files
            allowed_file: File containing allowed CONFIG options
            replace_list: List of prefix,adhoc tuples.  The prefix is stripped
                from each Kconfig and replaced with the adhoc string prior to
                comparison.  (e.e. ['PLATFORM_EC',''])
            use_defines: True if each line of the file starts with #define
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory

        Returns:
            Exit code: 0 if OK, 1 if a problem was found
        """
        new_adhoc, _, updated_adhoc = self.check_adhoc_configs(
            configs_file,
            srcdir,
            allowed_file,
            replace_list,
            use_defines,
            search_paths,
        )
        with open(NEW_ALLOWED_FNAME, "w", encoding="utf-8") as out:
            combined = sorted(new_adhoc + updated_adhoc)
            for config in combined:
                print(f"CONFIG_{config}", file=out)
        print(f"New list is in {NEW_ALLOWED_FNAME}")

    def check_undef(
        self,
        srcdir,
        search_paths,
    ):
        """Parse the ec header files and find zephyr Kconfigs that are
        incorrectly undefined or defined to a default value.

        Args:
            srcdir: Source directory to scan for Kconfig files
            search_paths: List of project paths to search for Kconfig files, in
                addition to the current directory

        Returns:
            Exit code: 0 if OK, 1 if a problem was found
        """
        kconfigs = set(
            self.scan_kconfigs(
                srcdir=srcdir, replace_list=None, search_paths=search_paths
            )
        )

        if_re = re.compile(r"^\s*#\s*if(ndef CONFIG_ZEPHYR)?")
        endif_re = re.compile(r"^\s*#\s*endif")
        modify_config_re = re.compile(r"^\s*#\s*(define|undef)\s+CONFIG_(\S*)")
        exit_code = 0
        files_to_check = glob.glob(
            os.path.join(srcdir, "include/**/*.h"), recursive=True
        )
        files_to_check += glob.glob(
            os.path.join(srcdir, "common/**/public/*.h"), recursive=True
        )
        files_to_check += glob.glob(
            os.path.join(srcdir, "driver/**/*.h"), recursive=True
        )
        for filename in files_to_check:
            with open(filename, "r", encoding="utf-8") as config_h:
                depth = 0
                ignore_depth = 0
                line_count = 0
                for line in config_h.readlines():
                    line_count += 1
                    line = line.strip("\n")
                    match = if_re.match(line)
                    if match:
                        depth += 1
                        if match[1] or ignore_depth > 0:
                            ignore_depth += 1
                    if endif_re.match(line):
                        if depth > 0:
                            depth -= 1
                        if ignore_depth > 0:
                            ignore_depth -= 1
                    if ignore_depth == 0:
                        match = modify_config_re.match(line)
                        if match:
                            if match[2] in kconfigs:
                                print(
                                    f"ERROR: Modifying CONFIG_{match[2]} "
                                    "outside of #ifndef CONFIG_ZEPHYR not "
                                    f"allowed at {filename}:{line_count}",
                                    file=sys.stderr,
                                )
                                exit_code = 1
        return exit_code


def main(argv):
    """Main function"""
    args = parse_args(argv)
    if not args.debug:
        sys.tracebacklimit = 0
    checker = KconfigCheck()

    # Sort the prefix,adhoc tuples by prefix length, longest first.
    # This ensures that the PLATFORM_EC_CONSOLE_CMD_ replacements
    # happen before the PLATFORM_EC_ replacement.
    replace_list = sorted(
        args.replace_list, reverse=True, key=lambda item: len(item[0])
    )

    if args.cmd == "check":
        return checker.do_check(
            configs_file=args.configs,
            srcdir=args.srctree,
            allowed_file=args.allowed,
            replace_list=replace_list,
            use_defines=args.use_defines,
            search_paths=args.search_path,
            ignore=args.ignore,
        )
    if args.cmd == "build":
        return checker.do_build(
            configs_file=args.configs,
            srcdir=args.srctree,
            allowed_file=args.allowed,
            replace_list=replace_list,
            use_defines=args.use_defines,
            search_paths=args.search_path,
        )
    if args.cmd == "check_undef":
        return checker.check_undef(
            srcdir=args.srctree,
            search_paths=args.search_path,
        )
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
