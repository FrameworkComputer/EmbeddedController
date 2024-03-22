#!/bin/bash
#
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generate a kernel include file from ec_commands.h.

usage() {
  cat << EOF
Generate an ec_commands.h file suitable for upstreaming to kernel.org.
Syntax:
$0 source_ec_commands.h target_cros_ec_commands.h

source_ec_commands.h: source file, usually include/ec_commands.h
target_cros_ec_commands.h: target file that will be upstreamed.
EOF
}

set -e

in="$1"
out="$2"

if [ $# -ne 2 ]; then
  usage
  exit 1
fi

out_dir="$(dirname "${out}")"
mkdir -p "${out_dir}"
tmp="$(mktemp -p "${out_dir}" cros_ec_XXX.h)"
cp "${in}" "${tmp}"

cleanup() {
  rm -f "${tmp}"*
}

trap cleanup EXIT

# Replace license
patch "${tmp}" << EOF
@@ -1,6 +1,11 @@
-/* Copyright 2014 The ChromiumOS Authors
- * Use of this source code is governed by a BSD-style license that can be
- * found in the LICENSE file.
+/* SPDX-License-Identifier: GPL-2.0-only */
+/*
+ * Host communication command constants for ChromeOS EC
+ *
+ * Copyright 2012 Google, Inc
+ *
+ * NOTE: This file is auto-generated from ChromeOS EC Open Source code from
+ * https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/include/ec_commands.h
  */

 /* Host communication command constants for Chrome EC */
EOF

# Change header guards
sed -i "s/__CROS_EC_EC_COMMANDS_H/__CROS_EC_COMMANDS_H/" "${tmp}"

# Convert UINT32_MAX into U32_MAX (and friends).
sed -i "s/UINT\([0-9]\{1,2\}\)_MAX/U\1_MAX/" "${tmp}"
sed -i "s/INT\([0-9]\{1,2\}\)_MAX/S\1_MAX/" "${tmp}"
sed -i "s/INT\([0-9]\{1,2\}\)_MIN/S\1_MIN/" "${tmp}"

# Remove non kernel code to prevent checkpatch warnings and simplify the .h.
unifdef -x2 -m -UCONFIG_HOSTCMD_ALIGNED -U__ACPI__ -D__KERNEL__ -U__cplusplus \
  -UCHROMIUM_EC "${tmp}"

cp "${tmp}" "${out}"
