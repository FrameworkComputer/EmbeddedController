# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for fingerprint sensor

# Note that this variable includes the trailing "/"
_fpsensor_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_dir)fpsensor_state.o
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_dir)fpsensor_crypto.o
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_dir)fpsensor.o
