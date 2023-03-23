# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for fingerprint sensor

# Only build if CONFIG_FINGERPRINT_MCU or TEST_BUILD is "y".
ifneq (,$(filter y,$(CONFIG_FINGERPRINT_MCU) $(TEST_BUILD)))

# Note that this variable includes the trailing "/"
_fpsensor_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Additional CFLAGS for only fpsensor objects
fpsensor_CFLAGS=-Wvla

_fpsensor_state_obj:=$(_fpsensor_dir)fpsensor_state.o
_fpsensor_crypto_obj:=$(_fpsensor_dir)fpsensor_crypto.o
_fpsensor_obj:=$(_fpsensor_dir)fpsensor.o
_fpsensor_detect_strings_obj:=$(_fpsensor_dir)fpsensor_detect_strings.o

$(out)/RW/$(_fpsensor_state_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_crypto_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_detect_strings_obj): CFLAGS+=$(fpsensor_CFLAGS)

all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_state_obj)
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_crypto_obj)
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_obj)
all-obj-$(HAS_TASK_CONSOLE)+=$(_fpsensor_detect_strings_obj)

endif # CONFIG_FINGERPRINT_MCU or TEST_BUILD
