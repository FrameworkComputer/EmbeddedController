#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

DATABASE="database.bin"
OUTFILE="build/tokens.bin"
PW_ROOT="../../third_party/pigweed"

find build -name "${DATABASE}" -print0 | xargs -0 \
  "${PW_ROOT}"/pw_tokenizer/py/pw_tokenizer/database.py \
  create --type binary --force --database "${OUTFILE}"
