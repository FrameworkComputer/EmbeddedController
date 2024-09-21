# -*- makefile -*-
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Helipilot baseboard specific files build
#

CHIP := npcx
CHIP_FAMILY := npcx9
CHIP_VARIANT := npcx9mfp

baseboard-y += base_board.o
baseboard-rw += base_board_rw.o
baseboard-y += fpsensor_detect.o
baseboard-rw += fpsensor_detect_rw.o

# Do not build rsa test because this board uses RSA exponent 3 and the rsa test
# will fail on device.
# TODO(b/314131076): Fix cortexm_fpu test for helipilot
# TODO(b/314131510): Fix stm32f_rtc test (or create a new one) for helipilot
test-list-y = \
       abort \
       aes \
       always_memset \
       assert_builtin \
       assert_stdlib \
       benchmark \
       boringssl_crypto \
       compile_time_macros \
       cortexm_fpu \
       crc \
       debug \
       exception \
       exit \
       flash_physical \
       flash_write_protect \
       fp_transport \
       fpsensor_auth_crypto_stateful \
       fpsensor_auth_crypto_stateful_otp \
       fpsensor_auth_crypto_stateless \
       fpsensor_crypto \
       fpsensor_debug \
       fpsensor_hw \
       fpsensor_utils \
       ftrapv \
       global_initialization \
       libc_printf \
       libcxx \
       malloc \
       mpu \
       mutex \
       mutex_trylock \
       mutex_recursive \
       otp_key \
       panic \
       panic_data \
       pingpong \
       printf \
       queue \
       ram_lock \
       restricted_console \
       rng_benchmark \
       rollback \
       rollback_entropy \
       rsa3 \
       rtc \
       rtc_npcx9 \
       sbrk \
       scratchpad \
       sha256 \
       sha256_unrolled \
       static_if \
       stdlib \
       std_vector \
       system_is_locked \
       timer \
       timer_dos \
       tpm_seed_clear \
       uart \
       unaligned_access \
       unaligned_access_benchmark \
       utils \
       utils_str

# This is relative to the EC root directory.
ifneq ($(BOARD_BUCCANEER),y)
-include ../ec-private/board/helipilot/build.mk
endif
