#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Download and install the latest Renode Debian package.

# Print the command and then run it.
msg-run() {
  echo -e "\E[1;32m> $*\E[m"
  "$@"
}

main() {
  local download_dir
  download_dir="$(mktemp -d "/tmp/renode-pkg-upgrade.XXX")"

  # Stable: https://github.com/renode/renode/releases
  # https://github.com/renode/renode/releases/download/v1.14.0/renode_1.14.0_amd64.deb

  # Nightlies: https://builds.renode.io/
  local url="https://builds.renode.io/renode-latest.deb"

  local deb="${download_dir}/renode.deb"

  local wget_opts=( -o - --progress=dot:giga )
  wget_opts+=( --compression=auto ) # default is "none"

  msg-run wget "${url}" -O "${deb}" "${wget_opts[@]}"
  echo

  msg-run sudo apt install -f "${deb}"
}

main "$@"
