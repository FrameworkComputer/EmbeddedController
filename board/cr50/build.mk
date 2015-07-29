# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

CHIP:=g
CHIP_FAMILY:=cr50
CHIP_VARIANT ?= cr50_fpga

board-y=board.o
LDFLAGS_EXTRA += -L$(out)/tpm2/build -ltpm2

# Need to generate a .hex file
all: hex

ifeq ($(BOARD_MK_INCLUDED),)
BOARD_MK_INCLUDED=1

$(out)/RO/ec.RO.elf: $(out)/tpm2/build/libtpm2.a
$(out)/RW/ec.RW.elf: $(out)/tpm2/build/libtpm2.a

.PHONY: $(out)/tpm2/build/libtpm2.a
$(out)/tpm2/build/libtpm2.a:
	rsync -a ../../third_party/tpm2 $(out)
	$(MAKE) ROOTDIR=$(realpath board/$(BOARD)/tpm2) EMBEDDED_MODE=1 \
	 -C $(out)/tpm2
endif
