#!/bin/bash
# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -e

PYTHON_FILES=()

for path in "$@"; do
    case "${path}" in
        *.py|*.pyi)
            PYTHON_FILES+=("${path}")
            ;;
        util/chargen)
            PYTHON_FILES+=("${path}")
            ;;
    esac
done

if [ "${#PYTHON_FILES}" -eq 0 ]; then
    # No python changes made, do nothing.
    exit 0
fi

EXIT_STATUS=0

# Wraps a black/isort command and reports how to fix it.
wrap_fix_msg() {
    local cmd="$1"
    shift

    if ! "${cmd}" "$@"; then
        cat <<EOF >&2
Looks like the ${cmd} formatter detected that formatting changes
need applied.  Fix by running this command from the platform/ec
directory and amending your changes:

  ${cmd} ${PYTHON_FILES[*]}

EOF
        EXIT_STATUS=1
    fi
}

# black and isort are provided by repo_tools
wrap_fix_msg black --check "${PYTHON_FILES[@]}"
wrap_fix_msg isort --check "${PYTHON_FILES[@]}"

exit "${EXIT_STATUS}"
