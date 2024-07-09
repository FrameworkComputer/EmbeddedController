#!/bin/bash
#
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

# Get the script's absolute directory path.
chromiumos_src_dir=$(dirname "$(realpath "$0")")

# Loop until we reach the root directory.
while [[ ${chromiumos_src_dir} != "/" ]]; do
  # Check if .repo directory exists.
  if [[ -d ${chromiumos_src_dir}/.repo ]]; then
    echo "${chromiumos_src_dir}"
    break
  fi
  # Go up one level in the directory tree.
  chromiumos_src_dir=$(dirname "${chromiumos_src_dir}")
done

: "${ZEPHYR_BASE:=${chromiumos_src_dir}/src/third_party/zephyr/main}"
TMP="$(mktemp -d)"
ec_commands_file_out="${TMP}/cros_ec_commands.h"

cleanup() {
  rm -rf "${TMP:?}"
}

trap cleanup EXIT

# Get unifdef from CIPD, and add to PATH
cipd ensure -ensure-file - -root "${TMP:?}" <<EOF
chromiumos/infra/tools/unifdef latest
EOF
export PATH="${TMP:?}/bin:${PATH}"

./util/make_linux_ec_commands_h.sh include/ec_commands.h \
  "${ec_commands_file_out}"

"${ZEPHYR_BASE}/scripts/checkpatch.pl" -f "${ec_commands_file_out}" \
  --ignore=BRACKET_SPACE

cleanup
