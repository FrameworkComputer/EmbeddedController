#!/bin/bash

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Tool to compare two commits and make sure that the resulting build output is
# exactly the same.

. /usr/share/misc/shflags

DEFINE_string 'board' "nocturne_fp" 'Board to build (\"all\" for all boards)' \
              'b'
DEFINE_string 'ref1' "HEAD" 'Git reference (commit, branch, etc)'
DEFINE_string 'ref2' "HEAD^" 'Git reference (commit, branch, etc)'

# Process commandline flags.
FLAGS "${@}" || exit 1
eval set -- "${FLAGS_ARGV}"

set -e

BOARD="${FLAGS_board}"
BOARDS_TO_SKIP="$(grep -E '^skip_boards =' Makefile.rules)"
BOARDS_TO_SKIP="${BOARDS_TO_SKIP//skip_boards = /}"
# Cr50 doesn't have reproducible builds.
# The following fails:
# git commit --allow-empty -m "Test" &&
# ./util/compare_build.sh --board cr50 --ref1 HEAD --ref2 HEAD^
BOARDS_TO_SKIP+=" cr50"

# Can specify any valid git ref (e.g., commits or branches).
OLD_REF="$(git rev-parse --short "${FLAGS_ref1}")"
NEW_REF="$(git rev-parse --short "${FLAGS_ref2}")"

SAVED_BRANCH="$(git rev-parse --abbrev-ref HEAD)"

on_exit() {
  # Intentionally not removing TMP_DIR on failure so that it can be
  # inspected.
  git checkout "${SAVED_BRANCH}" >/dev/null 2>&1
}
trap on_exit EXIT

do_build() {
  local ref="$1"
  local result_dir="$2"
  local board="$3"
  git checkout "${ref}" >/dev/null 2>&1
  echo "Testing commit: $(git rev-parse --short HEAD)"
  rm -rf "build/${board}"
  # STATIC_VERSION makes sure the generated ec_version.h is constant.
  # See util/getversion.sh.
  make STATIC_VERSION=1 V=1 BOARD="${board}" -j >./build.out 2>&1
  rm -rf "${result_dir}"
  mv "build/${board}" "${result_dir}"
  mv build.out "${result_dir}"
}

compare_board() {
  local board="$1"
  local old_ref="$2"
  local new_ref="$3"
  local tmp_dir="$4"

  local old_build_dir="${tmp_dir}/build.${board}_${old_ref}"
  local new_build_dir="${tmp_dir}/build.${board}_${new_ref}"

  echo "BOARD: ${board}, ${old_build_dir}, ${new_build_dir}"
  do_build "${old_ref}" "${old_build_dir}" "${board}"
  do_build "${new_ref}" "${new_build_dir}" "${board}"

  if ! diff "${old_build_dir}/ec.bin" "${new_build_dir}/ec.bin" >/dev/null 2>&1;
  then
    echo "ec.bin FAILURE"
    exit 1
  fi

  rm -rf "${old_build_dir}"
  rm -rf "${new_build_dir}"
}

if [[ "${BOARD}" == "all" ]]; then
  BOARD=""
  echo "Skipping boards: ${BOARDS_TO_SKIP}"
  for b in board/*; do
    b=${b//board\//}
    skipped=0
    echo "b: ${b}"
    for skip in ${BOARDS_TO_SKIP}; do
      if [[ "${skip}" == "${b}" ]]; then
        skipped=1
        break
      fi
    done
    if [[ ${skipped} == 0 ]]; then
      BOARD+=" ${b}"
    fi
  done
fi

echo "BOARD: ${BOARD}"

TMP_DIR="$(mktemp -d)"

for board in ${BOARD}; do
  compare_board "${board}" "${OLD_REF}" "${NEW_REF}" "${TMP_DIR}"
done

rm -rf "${TMP_DIR}"

echo "ec.bin MATCH"
