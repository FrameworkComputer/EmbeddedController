#!/bin/bash
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ "$#" -ne 2 ]]; then
  echo "Usage: $0 base_name variant_name"
  echo "e.g. $0 hatch kohaku"
  echo "Creates the initial EC image as a copy of the base board's EC."
  exit 1
fi

# This is the name of the base board that we're cloning to make the variant.
# ${var,,} converts to all lowercase
base="${1,,}"
# This is the name of the variant that is being cloned
variant="${2,,}"

# All of the necessary files are in the ../board directory:
pushd "${BASH_SOURCE%/*}/../board" || exit

# Make sure that the base exists
if [[ ! -e "${base}" ]]; then
  echo "${base} does not exist; please specify a valid baseboard"
  popd || exit
  exit 2
fi

# Make sure the variant doesn't already exist
if [[ -e "${variant}" ]]; then
  echo "${variant} already exists; have you already created this variant?"
  popd || exit
  exit 2
fi

# Start a branch. Use YMD timestamp to avoid collisions.
DATE=$(date +%Y%m%d)
repo start "create_${variant}_${DATE}" || exit

mkdir "${variant}"
cp "${base}"/* "${variant}"
# TODO replace the base name with the variant name in the copied files,
# TODO except for the BASEBOARD=${base^^} line in build.mk

# Build the code; exit if it fails
pushd .. || exit
make BOARD=${variant} || exit
popd || exit

git add "${variant}"/*

# Now commit the files
git commit -sm "${variant}: Initial EC image

The starting point for the ${variant} EC image

BUG=none
BRANCH=none
TEST=make BOARD=${variant}"

popd || exit

echo "Please check all the files (git show), make any changes you want,"
echo "and then repo upload."
