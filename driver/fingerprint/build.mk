# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build fingerprint drivers

# Note that this variable includes the trailing "/"
_fingerprint_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

include $(_fingerprint_cur_dir)elan/build.mk
include $(_fingerprint_cur_dir)fpc/build.mk
include $(_fingerprint_cur_dir)egis/build.mk
