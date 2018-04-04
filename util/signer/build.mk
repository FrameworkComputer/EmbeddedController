# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# When building self signed Cr50 images we still want the epoch/major/minor
# fields come from the dev manifest. Since a full blown JSON parser for C is
# not readily available, this rule generates a small .h file with only the
# fields of interest retrieved from the dev JSON file.
$(out)/pmjp.h: util/signer/pmjp.py util/signer/ec_RW-manifest-dev.json
	@echo "  PMJP $@"
	$(Q)./util/signer/pmjp.py ./util/signer/ec_RW-manifest-dev.json > $@
