#!/bin/sh
#
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# TODO: upstream patch: remove this file

grep -v -E '^\s*#include\s+<linux/compiler\.h>\s*$' | sed 's/ __user / /g'
