# -*- makefile -*-
# vim: set filetype=make :
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system - third_party rules/targets
#

ifeq ($(CONFIG_BORINGSSL_CRYPTO), y)
ifndef CMAKE_SYSTEM_PROCESSOR
$(error ERROR: Set CMAKE_SYSTEM_PROCESSOR in core/$(CORE)/toolchain.mk)
endif
ifndef OPENSSL_NO_ASM
$(error ERROR: Set OPENSSL_NO_ASM in core/$(CORE)/toolchain.mk)
endif

# The boringssl path can be overridden on invocation, as in the following
# example: $ make BORINGSSL_DIR=~/src/boringssl BOARD=bloonchipper
BORINGSSL_DIR ?= ../../third_party/boringssl

BORINGSSL_OUTDIR := $(out)/third_party/boringssl/crypto
BORINGSSL_TOOLCHAIN := \
	$(shell pwd)/third_party/boringssl/boringssl-toolchain.cmake

$(BORINGSSL_OUTDIR)/libcrypto.a:
	mkdir -p $(out)/third_party/boringssl/
	cmake \
		-DCC_NAME=$(CROSS_COMPILE_CC_NAME) \
		-DCXX_NAME=$(CROSS_COMPILE_CXX_NAME) \
		-DCROSS_COMPILE=$(CROSS_COMPILE) \
		-DCMAKE_SYSTEM_PROCESSOR=$(CMAKE_SYSTEM_PROCESSOR) \
		-DCMAKE_SYSROOT=$(SYSROOT) \
		-DOPENSSL_NO_ASM=$(OPENSSL_NO_ASM) \
		-DCROS_EC_REPO=$(CURDIR) \
		-DCMAKE_TOOLCHAIN_FILE=$(BORINGSSL_TOOLCHAIN) \
		-DCMAKE_VERBOSE_MAKEFILE=$(V) \
		-B $(out)/third_party/boringssl/ \
		-S $(BORINGSSL_DIR) \
		-GNinja
	cmake --build $(out)/third_party/boringssl/ -- crypto

# Make sure the EC/FPMCU code can link to the boringssl library.
CPPFLAGS += -I$(BORINGSSL_DIR) -I$(BORINGSSL_DIR)/include
BORINGSSL_LDFLAGS := -L$(BORINGSSL_OUTDIR) -lcrypto

# And the custom helpers.
CPPFLAGS += -I$(shell pwd)/third_party/boringssl/include

# Disable the unsupported features to prevent the usage of pthread & socket
# related types in headers.
CPPFLAGS += -DCROS_EC

$(out)/RO/ec.RO.elf $(out)/RO/ec.RO_B.elf: LDFLAGS_EXTRA += $(BORINGSSL_LDFLAGS)
$(out)/RO/ec.RO.elf $(out)/RO/ec.RO_B.elf: $(BORINGSSL_OUTDIR)/libcrypto.a
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: LDFLAGS_EXTRA += $(BORINGSSL_LDFLAGS)
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: $(BORINGSSL_OUTDIR)/libcrypto.a

$(out)/$(PROJECT).exe: LDFLAGS_EXTRA += $(BORINGSSL_LDFLAGS)
$(out)/$(PROJECT).exe: $(BORINGSSL_OUTDIR)/libcrypto.a

# On-device tests.
third-party-test-targets=$(foreach test,$(test-list-y),\
	$(out)/RW/$(test).RW.elf $(out)/RO/$(test).RO.elf)
$(third-party-test-targets): LDFLAGS_EXTRA += $(BORINGSSL_LDFLAGS)
$(third-party-test-targets): $(BORINGSSL_OUTDIR)/libcrypto.a
endif # CONFIG_BORINGSSL_CRYPTO

# Build and link against googletest in *test* builds if configured.
ifeq ($(TEST_BUILD),y)
ifeq ($(CONFIG_GOOGLETEST),y)

# The googletest path can be overridden on invocation. For example:
# $ make GOOGLETEST_DIR=~/src/googletest BOARD=bloonchipper
GOOGLETEST_DIR ?= $(realpath ../../third_party/googletest)
GOOGLETEST_INSTALL_DIR := $(realpath $(out))/googletest/install
CMAKE_TOOLCHAIN_FILE := $(CURDIR)/cmake/toolchain-armv7m.cmake

GOOGLETEST_CONFIG_CFLAGS := -DGTEST_HAS_FILE_SYSTEM=0
GOOGLETEST_CFLAGS := -I$(GOOGLETEST_INSTALL_DIR)/include \
	$(GOOGLETEST_CONFIG_CFLAGS)
GOOGLETEST_LDFLAGS := -L$(GOOGLETEST_INSTALL_DIR)/lib -lgtest -lgmock
GOOGLETEST_LIB := $(GOOGLETEST_INSTALL_DIR)/lib/libgtest.a

$(GOOGLETEST_LIB):
	mkdir -p $(out)/googletest && \
	cd $(out)/googletest && \
	CXXFLAGS="$(GOOGLETEST_CONFIG_CFLAGS)" cmake -Dgtest_disable_pthreads=ON \
		-GNinja \
		-DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TOOLCHAIN_FILE) \
		-DCMAKE_INSTALL_PREFIX=$(GOOGLETEST_INSTALL_DIR) \
		-DCMAKE_VERBOSE_MAKEFILE=$(V) \
		$(GOOGLETEST_DIR) && \
	cmake --build . && \
	cmake --install .

# On-device tests.
third-party-test-targets=$(foreach test,$(test-list-y),\
	$(out)/RW/$(test).RW.elf $(out)/RO/$(test).RO.elf)
$(third-party-test-targets): LDFLAGS_EXTRA += $(GOOGLETEST_LDFLAGS)
$(third-party-test-targets): CFLAGS += $(GOOGLETEST_CFLAGS)
$(third-party-test-targets): $(GOOGLETEST_LIB)

# Test files can include googletest headers, so the headers need to be
# installed first.
$(ro-objs): $(GOOGLETEST_LIB)
$(rw-objs): $(GOOGLETEST_LIB)

endif # CONFIG_GOOGLETEST
endif # TEST_BUILD
