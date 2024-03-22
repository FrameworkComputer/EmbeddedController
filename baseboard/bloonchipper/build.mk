# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F412
CHIP := stm32
CHIP_FAMILY := stm32f4
CHIP_VARIANT := stm32f412

# Don't forget that the board build.mk is included more than once to allow
# conditional variables to be realized. This means that we need to redefine all
# variable or the "+=" lines will compound.
baseboard-rw = base_board_rw.o
baseboard-y = base_board.o
baseboard-y += fpsensor_detect.o
baseboard-rw += fpsensor_detect_rw.o

# Do not build rsa test because this board uses RSA exponent 3 and the rsa test
# will fail on device.
test-list-y = \
       abort \
       aes \
       always_memset \
       benchmark \
       boringssl_crypto \
       compile_time_macros \
       cortexm_fpu \
       crc \
       debug \
       exception \
       flash_physical \
       flash_write_protect \
       fpsensor \
       fpsensor_auth_crypto_stateful \
       fpsensor_auth_crypto_stateless \
       fpsensor_hw \
       ftrapv \
       global_initialization \
       libc_printf \
       libcxx \
       malloc \
       mpu \
       mutex \
       mutex_trylock \
       mutex_recursive \
       panic \
       panic_data \
       pingpong \
       printf \
       queue \
       restricted_console \
       rng_benchmark \
       rollback \
       rollback_entropy \
       rsa3 \
       rtc \
       rtc_stm32f4 \
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
-include private/board/hatch_fp/build.mk
