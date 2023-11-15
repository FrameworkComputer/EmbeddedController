#!/bin/sh
#
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# TODO: upstream patch: remove this file

sed 's;^\(\s*#include\s\+\)<linux/\(i2c-pseudo\.h\)>\s*$;\1"\2";'
