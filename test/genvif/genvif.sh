#!/bin/bash -e
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cd test/genvif
make clean
make test
