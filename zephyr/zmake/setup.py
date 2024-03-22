# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configuration to allow pip install."""

import setuptools


setuptools.setup(
    name="zephyr-chrome-utils",
    version="0.1",
    description="CrOS Zephyr Utilities",
    long_description="Utilities used for working on a Zephyr-based EC",
    url="https://chromium.googlesource.com/chromiumos/platform/ec",
    author="ChromiumOS Authors",
    author_email="chromiumos-dev@chromium.org",
    license="BSD",
    # What does your project relate to?
    keywords="chromeos",
    # You can just specify the packages manually here if your project is
    # simple. Or you can use find_packages().
    packages=["zmake", "zephyr_build_tools"],
    python_requires=">=3.6, <4",
    # List run-time dependencies here.  These will be installed by pip when
    # your project is installed. For an analysis of "install_requires" vs pip's
    # requirements files see:
    # https://packaging.python.org/en/latest/requirements.html
    install_requires=[
        # Required until chroot upgrades to Python 3.7+.
        "dataclasses>=0.6; python_version < '3.7'",
    ],
    # To provide executable scripts, use entry points in preference to the
    # "scripts" keyword. Entry points provide cross-platform support and allow
    # pip to create the appropriate form of executable for the target platform.
    entry_points={
        "console_scripts": [
            "zmake=zmake.__main__:main",
        ],
    },
    extras_require={
        "tests": [
            "coverage",
            "pytest",
            "hypothesis",
            "testfixtures",
        ],
    },
)
