#!/bin/bash
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -e

AFFECTED_FILES=()

for path in "$@"; do
    case "${path}" in
        *zephyr/zmake/*.py | *zephyr/projects/*.py)
            AFFECTED_FILES+=("${path}")
            ;;
    esac
done

if [ "${#AFFECTED_FILES}" -eq 0 ]; then
    # No zmake changes made, do nothing.
    exit 0
fi

EXIT_STATUS=0

# Wraps a black/isort command and reports how to fix it.
wrap_fix_msg() {
    local cmd="$1"
    shift

    if ! "${cmd}" "$@"; then
        cat <<EOF >&2
Looks like zmake's ${cmd} formatter detected that formatting changes
need applied.  Fix by running this command from the zephyr/zmake
directory and amending your changes:

  ${cmd} .

EOF
        EXIT_STATUS=1
    fi
}

# We only want to run black, flake8, and isort inside of the chroot,
# as these are formatting tools which we want the specific versions
# provided by the chroot.
if [ -f /etc/cros_chroot_version ]; then
    cd "$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"
    wrap_fix_msg black --check --diff "${AFFECTED_FILES[@]}"
    wrap_fix_msg isort --check "${AFFECTED_FILES[@]}"
    flake8 "${AFFECTED_FILES[@]}" || EXIT_STATUS=1
    exit "${EXIT_STATUS}"
else
    cat <<EOF >&2
WARNING: It looks like you made zmake changes, but I'm running outside
of the chroot, and can't run zmake's auto-formatters.

It is recommended that you run repo upload from inside the chroot, or
you may see formatting errors during your CQ run.
EOF
    exit 1
fi
