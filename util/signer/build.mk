# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Lock library
#

signer_LIBS := -lcrypto
signer_ROOT := util/signer
SIGNER_DEPS := $(addprefix $(signer_ROOT)/, codesigner.cc \
	publickey.cc publickey.h signed_header.h)

$(out)/util/signer: $(SIGNER_DEPS)
	$(call quiet,cxx_to_host,HOSTCXX)

