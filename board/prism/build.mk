# -*- makefile -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f07x

# Build tests that we care about for Prism.
test-list-y=entropy rsa3 sha256 sha256_unrolled x25519

board-y=board.o
