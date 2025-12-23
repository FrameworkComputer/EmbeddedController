#!/bin/bash
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

cd "${SCRIPT_DIR}" || exit 1

if [ -f /etc/cros_chroot_version ]; then
        echo "Don't run this inside the chroot"
        exit 1
fi

go build && ./cq-excludes "$@"
