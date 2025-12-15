#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

SRC_DIR="$(realpath "$( dirname "${BASH_SOURCE[0]}" )/../../..")"
THIRD_PARTY_DIR="${SRC_DIR}/third_party"
echo "3p root directory: ${THIRD_PARTY_DIR}"

# Should match infra/config/misc_builders/copybot.star
declare -A repos=(
  ['zephyr/main']='https://github.com/zephyrproject-rtos/zephyr.git main'
  ['zephyr/cmsis']='https://github.com/zephyrproject-rtos/cmsis.git master'
  ['zephyr/cmsis_6']='https://github.com/zephyrproject-rtos/CMSIS_6.git main'
  ['zephyr/hal_intel_public']='https://github.com/zephyrproject-rtos/hal_intel.git main'
  ['zephyr/hal_stm32']='https://github.com/zephyrproject-rtos/hal_stm32.git main'
  ['zephyr/nanopb']='https://github.com/zephyrproject-rtos/nanopb.git zephyr'
  ['zephyr/picolibc']='https://github.com/zephyrproject-rtos/picolibc.git main'
  ['pigweed']='https://pigweed.googlesource.com/pigweed/pigweed main'
)

function die() {
  echo "$@"
  exit 1
}

for repo in "${!repos[@]}"; do
  read -ra upstream <<<"${repos[${repo}]}"
  upstream_repo="${upstream[0]}"
  upstream_branch="${upstream[1]}"

  cd "${THIRD_PARTY_DIR}/${repo}" || die "${THIRD_PARTY_DIR}/${repo} not found"
  repo start nodiffs . 2>/dev/null || die "repo start failed"
  git pull || die "git pull failed"
  upstream_commit="$(git log | sed -e '/^\s*GitOrigin-RevId:/!d' \
    -e 's/.*: //' -e 's/)$//' | head -1)"
  if [ "${upstream_commit}" == "" ]; then
    die "Could not find commit id to compare with"
  fi
  case "${upstream_commit}" in
    # cmsis has some commits out of order
    c3bd2094f92d574377f7af2aec147ae181aa5f8e)
      upstream_commit=4b96cbb174678dcd3ca86e11e1f24bc5f8726da0
      ;;
    # nanopb has some commits out of order
    0aa6f11bc7563989da85774a0decaecd3b304d6a)
      upstream_commit=65cbefb
      ;;
    # picolibc has a commit out of order
    b25f4a47784d2c24695977c903fe114565ae2bc6)
      upstream_commit=1c73900b79dbc02b80d09f5d637382249158e1ec
      ;;
  esac
  echo "==============================="
  echo -n "Diffing ${THIRD_PARTY_DIR}/${repo} vs "
  echo "${upstream_repo}@${upstream_branch}"
  git remote rm upstream >/dev/null
  git remote add -f upstream -t "${upstream_branch}" "${upstream_repo}" \
    >/dev/null 2>/dev/null || die "Failed to add upstream remote"

  echo "Starting diff at ${upstream_commit}"
  echo "Copybot missed commits:"
  git --no-pager log --no-decorate --format='%h %s %cr' \
    upstream/"${upstream_branch}" ^"${upstream_commit}" \
    || die "git log failed"
  echo "---------"

  git --no-pager diff "${upstream_commit}" ':(exclude).vpython3' \
    ':(exclude)DIR_METADATA' ':(exclude)OWNERS' ':(exclude)PRESUBMIT.cfg' \
    ':(exclude)cloudbuild_pigweed.yaml' ':(exclude)metadata/md5-cache/OWNERS' \
    || die "git diff failed"
done

exit 0
