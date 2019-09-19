#!/bin/bash
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ "$#" -lt 2 ]]; then
  echo "Usage: $0 base_name variant_name [bug_number]"
  echo "e.g. $0 hatch kohaku b:140261109"
  echo "Creates the initial EC image as a copy of the base board's EC."
  exit 1
fi

# This is the name of the base board that we're cloning to make the variant.
# ${var,,} converts to all lowercase.
BASE="${1,,}"
# This is the name of the variant that is being cloned.
VARIANT="${2,,}"

# Assign BUG= text, or "None" if that parameter wasn't specified.
BUG=${3:-None}

# All of the necessary files are in the ../board directory.
pushd "${BASH_SOURCE%/*}/../board" || exit

# Make sure that the base exists.
if [[ ! -e "${BASE}" ]]; then
  echo "${BASE} does not exist; please specify a valid baseboard"
  exit 1
fi

# Make sure the variant doesn't already exist.
if [[ -e "${VARIANT}" ]]; then
  echo "${VARIANT} already exists; have you already created this variant?"
  exit 1
fi

# Start a branch. Use YMD timestamp to avoid collisions.
DATE=$(date +%Y%m%d)
repo start "create_${VARIANT}_${DATE}" . || exit 1

mkdir "${VARIANT}"
cp "${BASE}"/* "${VARIANT}"
# TODO replace the base name with the variant name in the copied files,
# TODO except for the BASEBOARD=${BASE^^} line in build.mk.

# Build the code; exit if it fails.
pushd .. || exit 1
make BOARD=${VARIANT} || exit 1
popd || exit 1

git add "${VARIANT}"/*

# Now commit the files.
git commit -sm "${VARIANT}: Initial EC image

The starting point for the ${VARIANT} EC image

BUG=${BUG}
BRANCH=none
TEST=make BOARD=${VARIANT}"

echo "Please check all the files (git show), make any changes you want,"
echo "and then repo upload."
