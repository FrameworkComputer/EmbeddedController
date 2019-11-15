#!/bin/bash
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# NIST toolset needs sudo emerge dev-libs/libdivsufsort
rm -rf /tmp/ea
git clone --depth 1 https://github.com/usnistgov/SP800-90B_EntropyAssessment.git /tmp/ea/
make -C /tmp/ea/cpp/ non_iid
make -C /tmp/ea/cpp/ restart
TRNG_OUT=/tmp/trng_output
rm -f $TRNG_OUT
./tpmtest.py -t
if [ ! -f "$TRNG_OUT" ]; then
    echo "$TRNG_OUT does not exist"
    exit 1
fi
/tmp/ea/cpp/ea_non_iid -a $TRNG_OUT | tee ea_non_iid.log
entropy=`grep min ea_non_iid.log | awk '{ print $5 }'`
echo "Minimal entropy" $entropy
/tmp/ea/cpp/ea_restart $TRNG_OUT $entropy | tee -a ea_non_iid.log
