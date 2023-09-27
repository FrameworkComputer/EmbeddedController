# Copyright 2015 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from setuptools import setup


setup(
    name="ecusb",
    version="1.0",
    author="Nick Sanders",
    author_email="nsanders@chromium.org",
    url="https://www.chromium.org/chromium-os/ec-development",
    package_dir={"": "extra/tigertool"},
    packages=["ecusb"],
    description="Tiny implementation of servod.",
)

setup(
    name="powerlog",
    version="1.0",
    author="Nick Sanders",
    author_email="nsanders@chromium.org",
    url="https://www.chromium.org/chromium-os/ec-development",
    package_dir={"": "extra/usb_power"},
    py_modules=["powerlog", "stats_manager"],
    entry_points={
        "console_scripts": ["powerlog=powerlog:main"],
    },
    description="Sweetberry power logger.",
)

setup(
    name="console",
    version="1.0",
    author="Nick Sanders",
    author_email="nsanders@chromium.org",
    url="https://www.chromium.org/chromium-os/ec-development",
    package_dir={"": "extra/usb_serial"},
    py_modules=["console"],
    entry_points={
        "console_scripts": ["usb_console=console:main"],
    },
    description="Tool to open the usb console on servo, cr50.",
)

setup(
    name="unpack_ftb",
    version="1.0",
    author="Wei-Han Chen",
    author_email="stimim@chromium.org",
    url="https://www.chromium.org/chromium-os/ec-development",
    package_dir={"": "util"},
    py_modules=["unpack_ftb"],
    entry_points={
        "console_scripts": ["unpack_ftb=unpack_ftb:main"],
    },
    description="Tool to convert ST touchpad .ftb file to .bin",
)
