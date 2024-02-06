# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Helipilot board specific files build
#

BASEBOARD:=helipilot

board-y=

# Note that this variable includes the trailing "/"
_hatch_fp_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))
-include $(_hatch_fp_cur_dir)../../private/board/helipilot/build.mk
