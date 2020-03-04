#!/bin/bash

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Tool to merge master branch into a release branch. Currently specific to the
# fingerprint sensor, but can easily be generalized. See
# http://go/cros-fingerprint-firmware-branching-and-signing.

. /usr/share/misc/shflags

DEFINE_string 'board' "bloonchipper" 'EC board (FPMCU) to update' 'b'

# Process commandline flags.
FLAGS "${@}" || exit 1
eval set -- "${FLAGS_ARGV}"

set -e

# Dereference symlinks so "git log" works as expected.
readonly BOARD_DIR="$(realpath --relative-to=. "board/${FLAGS_board}")"
readonly RELEVANT_PATHS=(
  "${BOARD_DIR}"
  common/fpsensor
  docs/fingerprint
  driver/fingerprint
  util/getversion.sh
)
readonly RELEASE_BRANCH="firmware-fpmcu-${FLAGS_board}-release"

get_relevant_commits_cmd() {
  local head="${1}"
  local merge_head="${2}"
  local format="${3}"

  local relevant_commits_cmd="git log ${format} ${head}..${merge_head}"
  relevant_commits_cmd+=" -- ${RELEVANT_PATHS[*]}"
  echo "${relevant_commits_cmd}"
}

git_commit_msg() {
  local branch="${1}"
  local head="${2}"
  local merge_head="${3}"

  local relevant_commits_cmd
  local relevant_commits
  local relevant_bugs_cmd
  local relevant_bugs

  relevant_commits_cmd="$(get_relevant_commits_cmd "${head}" \
    "${merge_head}" "--oneline")"
  relevant_commits="$(${relevant_commits_cmd})"
  relevant_bugs_cmd="$(get_relevant_commits_cmd "${head}" \
    "${merge_head}" "")"
  relevant_bugs="$(${relevant_bugs_cmd} | \
    pcregrep -o1 'BUG=(.*)' | pcregrep -v none | \
    tr ' ' '\n' | tr ',' '\n' | \
    sort | uniq | xargs)"

  cat <<HEREDOC
Merge remote-tracking branch 'm/master' into ${branch}

Relevant changes:

${relevant_commits_cmd}

${relevant_commits}

BRANCH=none
BUG=${relevant_bugs}
TEST=test_that --board <board> <IP> suite:fingerprint
HEREDOC
}

merge_master() {
  git remote update
  git checkout -B "${RELEASE_BRANCH}" "cros/${RELEASE_BRANCH}"
  git merge --no-ff --no-commit m/master

  local branch
  local head
  local merge_head

  branch="$(git rev-parse --abbrev-ref HEAD)"
  head="$(git rev-parse --short HEAD)"
  merge_head="$(git rev-parse --short MERGE_HEAD)"

  git commit --signoff -m "$(git_commit_msg "${branch}" \
    "${head}" "${merge_head}")"
  git commit --amend
}

merge_master
