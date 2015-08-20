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

# Need to generate a .hex file
all: hex

# This file is included twice by the Makefile, once to determine the CHIP info
# and then again after defining all the CONFIG_ and HAS_TASK variables. We use
# a guard so that recipe definitions and variable extensions only happen the
# second time.
ifeq ($(BOARD_MK_INCLUDED_ONCE),)
BOARD_MK_INCLUDED_ONCE=1
else

# Build and link with an external library
EXTLIB := $(realpath ../../third_party/tpm2)

CFLAGS += -I$(EXTLIB) -I$(realpath $(BDIR)/tpm2)
LDFLAGS_EXTRA += -L$(out)/tpm2 -ltpm2

$(out)/RO/ec.RO.elf: $(out)/tpm2/libtpm2.a
$(out)/RW/ec.RW.elf: $(out)/tpm2/libtpm2.a

# Force the external build each time, so it can look for changed sources.
.PHONY: $(out)/tpm2/libtpm2.a
$(out)/tpm2/libtpm2.a:
	$(MAKE) obj=$(realpath $(out))/tpm2 EMBEDDED_MODE=1 -C $(EXTLIB)

endif   # BOARD_MK_INCLUDED_ONCE is nonempty
