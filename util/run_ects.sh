#!/bin/bash
#
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Run all eCTS tests and publish results

set -e

NOC='\033[0m'
RED='\033[0;31m'
GRN='\033[0;32m'

# List of tests to run.
TESTS=(meta gpio hook interrupt task timer)

usage() {
  cat <<END

${SCRIPT_NAME} - Run all eCTS tests and publish results.
Usage: ${SCRIPT_NAME} [options]
Options:
  -d: Dry run tests.
  -h: Print this message.
  -s: Sync tree before running tests.
  -u: Upload results.
  -v: Enable verbose output.

END
}

error() {
  printf "%b[%s] %s%b\n" "${RED}" "${SCRIPT_NAME}" "$*" "${NOC}" 1>&2
}

info() {
  printf "%b[%s] %s%b\n" "${GRN}" "${SCRIPT_NAME}" "$*" "${NOC}"
}

get_script_name() {
  local name=$(basename "$1")
  printf "${name%.*}"
}

get_ec_dir() {
  readlink -f "$(dirname $1)/.."
}

get_cros_sdk() {
  if [[ -e "/etc/cros_chroot_version" ]]; then
    printf ""
  else
    printf "cros_sdk --"
  fi
}

sync_src() {
  info "Syncing tree..."
  if ! repo sync .; then
    error "Failed to sync source"
    exit 1
  fi
}

run_test() {
  ${CROS_SDK} ${DRY_RUN} "/mnt/host/source/src/platform/ec/cts/cts.py" -m "$1"
}

run() {
  local t
  for t in "${TESTS[@]}"; do
    info "Running ${t} test"
    run_test "${t}"
  done
}

upload_results() {
  info "Uploading results... (Not implemented)"
}

main() {
  local do_sync
  local do_upload

  SCRIPT_NAME=$(get_script_name "$0")
  CROS_SDK=$(get_cros_sdk)
  EC_DIR=$(get_ec_dir "$0")
  DRY_RUN=""
  VERBOSITY=""

  # Need to cd to SDK directory to run tools (cros_sdk, repo sync).
  cd "${EC_DIR}"

  while getopts ":dhsuv" opt; do
    case "${opt}" in
      d) DRY_RUN="echo" ;;
      h)
        usage
        exit 0
        ;;
      s) do_sync="y" ;;
      u) do_upload="y" ;;
      v) VERBOSITY="y" ;;
      \?)
        error "invalid option: -${OPTARG}"
        exit 1
        ;;
    esac
  done
  shift $((OPTIND-1))

  [[ "${do_sync}" == "y" ]] && sync_src
  run
  [[ "${do_upload}" == "y" ]] && upload_results
}

main "$@"