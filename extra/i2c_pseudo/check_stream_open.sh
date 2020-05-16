#!/bin/sh
#
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This checks whether stream_open symbol is available from the target kernel.
#
# Output meanings:
#   -1 : stream_open is not available
#    0 : unknown whether or not stream_open is available
#    1 : stream_open is available

symbols="$(< "/lib/modules/$(uname -r)/build/Module.symvers" \
           awk '{print $2}' | grep -E '^(nonseekable_open|stream_open)$')"

if echo "${symbols}" | grep -q '^stream_open$'; then
	echo 1
elif echo "${symbols}" | grep -q '^nonseekable_open$'; then
	echo -1
else
	echo 0
fi
