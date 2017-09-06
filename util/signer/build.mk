# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

signer_LIBS := -lcrypto -lelf -lusb-1.0 -lxml2
signer_ROOT := util/signer
signer_INC := $(addprefix common/, aes.h  ecdh.h  gnubby.h  \
	image.h  publickey.h  signed_header.h)
signer_SRC := codesigner.cc publickey.cc image.cc gnubby.cc aes.cc ecdh.cc
SIGNER_DEPS := $(addprefix $(signer_ROOT)/, $(signer_SRC) $(signer_INC))

HOST_CXXFLAGS += -I/usr/include/libxml2 -I $(out)
$(out)/util/signer: $(SIGNER_DEPS)  $(out)/pmjp.h
	$(call quiet,cxx_to_host,HOSTCXX)

# When building self signed Cr50 images we still want the epoch/major/minor
# fields come from the dev manifest. Since a full blown JSON parser for C is
# not readily available, this rule generates a small .h file with only the
# fields of interest retrieved from the dev JSON file.
$(out)/pmjp.h: util/signer/pmjp.py util/signer/ec_RW-manifest-dev.json
	@echo "  PMJP $@"
	$(Q)./util/signer/pmjp.py ./util/signer/ec_RW-manifest-dev.json > $@
