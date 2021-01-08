# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Registry of known Zephyr modules."""

import pathlib
import os

import zmake.build_config as build_config
import zmake.util as util


def third_party_module(name, checkout, version):
    """Common callback in registry for all third_party/zephyr modules.

    Args:
        name: The name of the module.
        checkout: The path to the chromiumos source.
        version: The zephyr version.

    Return:
        The path to the module module.
    """
    if not version or len(version) < 2:
        return None
    return checkout / 'src' / 'third_party' / 'zephyr' / name / 'v{}.{}'.format(
        version[0], version[1])


known_modules = {
    'hal_stm32': third_party_module,
    'cmsis': third_party_module,
    'ec-shim': lambda name, checkout, version: (
        checkout / 'src' / 'platform' / 'ec'),
    'zephyr-chrome': lambda name, checkout, version: (
        checkout / 'src' / 'platform' / 'zephyr-chrome'),
}


def locate_modules(checkout_dir, version, modules=known_modules):
    """Resolve module locations from a known_modules dictionary.

    Args:
        checkout_dir: The path to the chromiumos source.
        version: The zephyr version, as a two or three tuple of ints.
        modules: The known_modules dictionary to use for resolution.

    Returns:
        A dictionary mapping module names to paths.
    """
    result = {}
    for name, locator in known_modules.items():
        result[name] = locator(name, checkout_dir, version)
    return result


def setup_module_symlinks(output_dir, modules):
    """Setup a directory with symlinks to modules.

    Args:
        output_dir: The directory to place the symlinks in.
        modules: A dictionary of module names mapping to paths.

    Returns:
        The resultant BuildConfig that should be applied to use each
        of these modules.
    """
    if not output_dir.exists():
        output_dir.mkdir(parents=True)

    module_links = []

    for name, path in modules.items():
        link_path = output_dir.resolve() / name
        util.update_symlink(path, link_path)
        module_links.append(link_path)

    if module_links:
        return build_config.BuildConfig(
            cmake_defs={'ZEPHYR_MODULES': ';'.join(map(str, module_links))})
    else:
        return build_config.BuildConfig()
