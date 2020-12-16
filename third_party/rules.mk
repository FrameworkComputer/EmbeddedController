# -*- makefile -*-
# vim: set filetype=make :
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system - third_party rules/targets
#

# Build and link against libcryptoc.
# See https://chromium.googlesource.com/chromiumos/third_party/cryptoc .
ifeq ($(CONFIG_LIBCRYPTOC),y)

# The cryptoc path can be overridden on invocation, as in the following example:
# $ make CRYPTOC_DIR=~/src/cryptoc BOARD=bloonchipper
CRYPTOC_DIR ?= $(realpath ../../third_party/cryptoc)

# SUPPORT_UNALIGNED indicates to libcryptoc that provided data buffers
# may be unaligned and please handle them safely.
cmd_libcryptoc = $(MAKE) -C $(CRYPTOC_DIR) \
	obj=$(realpath $(out))/cryptoc \
	SUPPORT_UNALIGNED=1
cmd_libcryptoc_clean = $(cmd_libcryptoc) -q && echo clean

ifneq ($(BOARD),host)
CPPFLAGS += -I$(abspath ./builtin)
endif
CPPFLAGS += -I$(CRYPTOC_DIR)/include
CRYPTOC_LDFLAGS := -L$(out)/cryptoc -lcryptoc

# Conditionally force the rebuilding of libcryptoc.a only if it would be
# changed.
# Note, we use ifndef to ensure the likelyhood of rebuilding is much higher.
# For example, if variable cmd_libcryptoc_clean is modified or blank,
# we will rebuild.
ifneq ($(shell $(cmd_libcryptoc_clean)),clean)
.PHONY: $(out)/cryptoc/libcryptoc.a
endif
$(out)/cryptoc/libcryptoc.a:
	+$(call quiet,libcryptoc,MAKE   )

# Link RO and RW against cryptoc.
$(out)/RO/ec.RO.elf $(out)/RO/ec.RO_B.elf: LDFLAGS_EXTRA += $(CRYPTOC_LDFLAGS)
$(out)/RO/ec.RO.elf $(out)/RO/ec.RO_B.elf: $(out)/cryptoc/libcryptoc.a
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: LDFLAGS_EXTRA += $(CRYPTOC_LDFLAGS)
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: $(out)/cryptoc/libcryptoc.a
# Host test executables (including fuzz tests).
$(out)/$(PROJECT).exe: LDFLAGS_EXTRA += $(CRYPTOC_LDFLAGS)
$(out)/$(PROJECT).exe: $(out)/cryptoc/libcryptoc.a
# On-device tests.
test-targets=$(foreach test,$(test-list-y),\
	$(out)/RW/$(test).RW.elf $(out)/RO/$(test).RO.elf)
$(test-targets): LDFLAGS_EXTRA += $(CRYPTOC_LDFLAGS)
$(test-targets): $(out)/cryptoc/libcryptoc.a

endif # CONFIG_LIBCRYPTOC
