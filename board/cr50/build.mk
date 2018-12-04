# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board-specific build requirements

# Define the SoC used by this board
CHIP:=g
CHIP_FAMILY:=cr50
CHIP_VARIANT ?= cr50_fpga

# This file is included twice by the Makefile, once to determine the CHIP info
# and then again after defining all the CONFIG_ and HAS_TASK variables. We use
# a guard so that recipe definitions and variable extensions only happen the
# second time.
ifeq ($(BOARD_MK_INCLUDED_ONCE),)
BOARD_MK_INCLUDED_ONCE=1
SIG_EXTRA = --cros
else

# Need to generate a .hex file
all: hex

# The simulator components have their own subdirectory
CFLAGS += -I$(realpath chip/$(CHIP)/dcrypto)
CFLAGS += -I$(realpath $(BDIR)/tpm2)
dirs-y += chip/$(CHIP)/dcrypto
dirs-y += $(BDIR)/tpm2

# Objects that we need to build
board-y =  board.o
board-y += ap_state.o
board-y += ec_state.o
board-y += power_button.o
board-y += servo_state.o
board-y += ap_uart_state.o
board-y += factory_mode.o
board-${CONFIG_RDD} += rdd.o
board-${CONFIG_USB_SPI} += usb_spi.o
board-${CONFIG_USB_I2C} += usb_i2c.o
board-y += recovery_button.o
board-y += tpm2/NVMem.o
board-y += tpm2/aes.o
board-y += tpm2/ecc.o
board-y += tpm2/ecies.o
board-y += tpm2/endorsement.o
board-y += tpm2/hash.o
board-y += tpm2/hash_data.o
board-y += tpm2/hkdf.o
board-y += tpm2/manufacture.o
board-y += tpm2/nvmem_ops.o
board-y += tpm2/platform.o
board-y += tpm2/rsa.o
board-y += tpm2/stubs.o
board-y += tpm2/tpm_mode.o
board-y += tpm2/tpm_state.o
board-y += tpm2/trng.o
board-y += tpm2/virtual_nvmem.o
board-y += tpm_nvmem_ops.o
board-y += wp.o
board-$(CONFIG_U2F) += u2f.o

# Build and link with an external library
EXTLIB := $(realpath ../../third_party/tpm2)
CFLAGS += -I$(EXTLIB)

# For the benefit of the tpm2 library.
INCLUDE_ROOT := $(abspath ./include)
CFLAGS += -I$(INCLUDE_ROOT)
CPPFLAGS += -I$(abspath ./builtin)
CPPFLAGS += -I$(abspath ./chip/$(CHIP))
# For core includes
CPPFLAGS += -I$(abspath .)
CPPFLAGS += -I$(abspath $(BDIR))
CPPFLAGS += -I$(abspath ./fuzz)
CPPFLAGS += -I$(abspath ./test)
ifeq ($(CONFIG_UPTO_SHA512),y)
CPPFLAGS += -DSHA512_SUPPORT
endif

# Make sure the context of the software sha512 implementation fits. If it ever
# increases, a compile time assert will fire in tpm2/hash.c.
ifeq ($(CONFIG_UPTO_SHA512),y)
CFLAGS += -DUSER_MIN_HASH_STATE_SIZE=208
else
CFLAGS += -DUSER_MIN_HASH_STATE_SIZE=112
endif
# Configure TPM2 headers accordingly.
CFLAGS += -DEMBEDDED_MODE=1
# Configure cryptoc headers to handle unaligned accesses.
CFLAGS += -DSUPPORT_UNALIGNED=1

# Add dependencies on that library
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: LDFLAGS_EXTRA += -L$(out)/tpm2 -ltpm2
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: $(out)/tpm2/libtpm2.a

#$(out)/RW/ec.RW_B.elf: $(out)/tpm2/libtpm2.a LDFLAGS_EXTRA += -L$(out)/tpm2 -ltpm2

# Force the external build each time, so it can look for changed sources.
.PHONY: $(out)/tpm2/libtpm2.a
$(out)/tpm2/libtpm2.a:
	$(MAKE) obj=$(realpath $(out))/tpm2 EMBEDDED_MODE=1 OBJ_PREFIX=Tpm2_ -C $(EXTLIB)

endif   # BOARD_MK_INCLUDED_ONCE is nonempty

board-$(CONFIG_PINWEAVER)+=pinweaver_tpm_imports.o
