# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for fingerprint sensor

# Only build if CONFIG_FINGERPRINT_MCU is "y".
ifneq (,$(filter y,$(CONFIG_FINGERPRINT_MCU)))

# Note that this variable includes the trailing "/"
_fpsensor_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Additional CFLAGS for only fpsensor objects
fpsensor_CFLAGS=-Wvla

_fpsensor_state_obj:=$(_fpsensor_dir)fpsensor_state.o
_fpsensor_crypto_obj:=$(_fpsensor_dir)fpsensor_crypto.o
_fpsensor_obj:=$(_fpsensor_dir)fpsensor.o
_fpsensor_detect_strings_obj:=$(_fpsensor_dir)fpsensor_detect_strings.o
_fpsensor_debug_obj:=$(_fpsensor_dir)fpsensor_debug.o
_fpsensor_utils_obj:=$(_fpsensor_dir)fpsensor_utils.o
_fpsensor_auth_commands_obj:=$(_fpsensor_dir)fpsensor_auth_commands.o
_fpsensor_auth_crypto_stateful_obj:=$(_fpsensor_dir)fpsensor_auth_crypto_stateful.o
_fpsensor_auth_crypto_stateless_obj:=$(_fpsensor_dir)fpsensor_auth_crypto_stateless.o

$(out)/RW/$(_fpsensor_state_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_crypto_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_detect_strings_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_debug_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_utils_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_auth_commands_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_auth_crypto_stateful_obj): CFLAGS+=$(fpsensor_CFLAGS)
$(out)/RW/$(_fpsensor_auth_crypto_stateless_obj): CFLAGS+=$(fpsensor_CFLAGS)

all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_state_obj)
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_obj)
all-obj-$(HAS_TASK_CONSOLE)+=$(_fpsensor_detect_strings_obj)
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_debug_obj)
all-obj-$(HAS_TASK_FPSENSOR)+=$(_fpsensor_auth_commands_obj)

# Since we only include the FPSENSOR task in the RW image, HAS_TASK_FPSENSOR
# will either be "rw" or empty.
fpsensor_obj_image=$(HAS_TASK_FPSENSOR)

ifeq ($(TEST_BUILD),y)
# The "emulator" (TEST_BUILD=y with BOARD=host) runs the tests from the RO
# image. For simplicity, the on-device tests (TEST_BUILD=y BOARD=bloonchipper)
# also build the tests for RO (but we run them in RW).
fpsensor_obj_image=y
endif

all-obj-$(fpsensor_obj_image)+=$(_fpsensor_utils_obj)
all-obj-$(fpsensor_obj_image)+=$(_fpsensor_auth_crypto_stateless_obj)
all-obj-$(fpsensor_obj_image)+=$(_fpsensor_crypto_obj)
all-obj-$(fpsensor_obj_image)+=$(_fpsensor_auth_crypto_stateful_obj)

endif # CONFIG_FINGERPRINT_MCU
