#!/bin/bash
#
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script converts input binary blob into output .h file,
#
# The three command line arguments are:
#
#  - name of the variable to define in the output .h file
#  - input binary blob to be converted to hex ASCII
#  - name of the output file
#
# The output file contains a C #define statement assigning the variable to hex
# dump of the input file.
#
# This script is supposed to be invoked from the make file, no command line
# argument verification is done.

# Make sure the user is alerted if not enough command line arguments are
# supplied.
set -u

variable_name="${1}"
input_file="${2}"
output_file="${3}"

key_dump="$(od -An -tx1 -w8 "${input_file}" | \
    sed 's/^ /\t0x/;s/ /, 0x/g;s/$/, \\/')"

cat > "${output_file}" <<EOF
/*
 * This is a generated file, do not edit.
 */

#define ${variable_name} { \\
${key_dump}
}

EOF
