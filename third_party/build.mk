# -*- makefile -*-
# vim: set filetype=make :
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The eigen3 path can be overridden on invocation, as in the following example:
# $ make EIGEN3_DIR=~/src/eigen3 BOARD=bloonchipper-druid
EIGEN3_DIR ?= ../../third_party/eigen3
