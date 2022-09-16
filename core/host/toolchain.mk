# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CROSS_COMPILE_HOST_DEFAULT:=$(if \
  $(shell which x86_64-pc-linux-gnu-$(cc-name) 2>/dev/null), \
  x86_64-pc-linux-gnu-,)

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_host),\
	$(CROSS_COMPILE_HOST_DEFAULT))
