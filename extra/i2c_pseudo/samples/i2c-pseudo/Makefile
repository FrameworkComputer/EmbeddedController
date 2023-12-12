# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# TODO: upstream patch: replace this file with CL:4867275 version
# (git commit 06639bfea933c889446935712fe0ce93f319c165)

EXAMPLE_PROG := i2c-adapter-example
EXAMPLE_SRC := $(EXAMPLE_PROG).c
MODIFIED_INCLUDE_SRC := $(EXAMPLE_PROG)-modified-include.c
UAPI_HEADER_NAME := i2c-pseudo.h
UAPI_HEADER_PATH := ../../include/uapi/linux/$(UAPI_HEADER_NAME)

.PHONY: clean

.DELETE_ON_ERROR:

$(EXAMPLE_PROG): $(MODIFIED_INCLUDE_SRC) i2c-pseudo.h
	cc -Wall -o $(EXAMPLE_PROG) $(MODIFIED_INCLUDE_SRC)

$(MODIFIED_INCLUDE_SRC): $(EXAMPLE_SRC) make_modified_include.sh
	./make_modified_include.sh < $(EXAMPLE_SRC) > $(MODIFIED_INCLUDE_SRC)

$(UAPI_HEADER_NAME): $(UAPI_HEADER_PATH) make_uapi_header.sh
	./make_uapi_header.sh < $(UAPI_HEADER_PATH) > $(UAPI_HEADER_NAME)

clean:
	$(RM) $(EXAMPLE_PROG) $(MODIFIED_INCLUDE_SRC) $(UAPI_HEADER_NAME)
