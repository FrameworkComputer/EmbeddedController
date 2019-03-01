# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# fuzzer binaries
#

fuzz-test-list-host =
# Fuzzers should only be built for architectures that support sanitizers.
ifeq ($(ARCH),amd64)
# TODO(crbug.com/911310) Fix the chromeos-ec build before adding cr50_fuzz back.
fuzz-test-list-host += cr50_fuzz host_command_fuzz usb_pd_fuzz
endif

# For fuzzing targets libec.a is built from the ro objects and hides functions
# that collide with stdlib. The rw only objects are then linked against libec.a
# with stdlib support. Therefore fuzzing targets that need to call this internal
# functions should be marked "-y" or "-ro", and fuzzing targets that need stdlib
# should be marked "-rw". In other words:
#
# Does your object file need to link against the Cr50 implementations of stdlib
# functions?
#   Yes -> use <obj_name>-y
# Does your object file need to link against cstdlib?
#   Yes -> use <obj_name>-rw
# Otherwise use <obj_name>-y
cr50_fuzz-rw = cr50_fuzz.o pinweaver_model.o mem_hash_tree.o nvmem_tpm2_mock.o
host_command_fuzz-y = host_command_fuzz.o
usb_pd_fuzz-y = usb_pd_fuzz.o

CR50_PROTO_HEADERS := $(out)/gen/fuzz/cr50_fuzz.pb.h \
  $(out)/gen/fuzz/pinweaver/pinweaver.pb.h
$(out)/RW/fuzz/pinweaver_model.o: ${CR50_PROTO_HEADERS}
$(out)/RW/fuzz/cr50_fuzz.o: ${CR50_PROTO_HEADERS}
$(out)/RW/fuzz/cr50_fuzz.o: CPPFLAGS+=${LIBPROTOBUF_MUTATOR_CFLAGS}

TPM2_LIB_ROOT := $(CROS_WORKON_SRCROOT)/src/third_party/tpm2
$(out)/RW/fuzz/nvmem_tpm2_mock.o: CFLAGS += -I$(TPM2_LIB_ROOT)

$(out)/cr50_fuzz.exe: $(out)/cryptoc/libcryptoc.a \
  $(out)/gen/fuzz/cr50_fuzz.pb.o \
  $(out)/gen/fuzz/pinweaver/pinweaver.pb.o \

$(out)/cr50_fuzz.exe: LDFLAGS_EXTRA+=-lcrypto ${LIBPROTOBUF_MUTATOR_LDLIBS}
