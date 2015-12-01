# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from setuptools import setup

setup(
    name="ec3po",
    version="1.0.0rc1",
    author="Aseda Aboagye",
    author_email="aaboagye@chromium.org",
    url="https://www.chromium.org/chromium-os/ec-development",
    package_dir={"" : "util"},
    packages=["ec3po"],
    py_modules=["ec3po.console", "ec3po.interpreter"],
    description="EC console interpreter.",
)
