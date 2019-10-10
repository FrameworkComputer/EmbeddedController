#!/bin/bash
#
# Copyright 2019 The Chromium OS Authors. All rights reserved.
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

if [ ! -d "${CROS_WORKON_SRCROOT}" ]; then
  printf "Not in Chrome OS chroot!\n\n"
  usage
  exit 0
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
-/* Copyright 2014 The Chromium OS Authors. All rights reserved.
- * Use of this source code is governed by a BSD-style license that can be
- * found in the LICENSE file.
+/* SPDX-License-Identifier: GPL-2.0 */
+/*
+ * Host communication command constants for ChromeOS EC
+ *
+ * Copyright (C) 2012 Google, Inc
+ *
+ * NOTE: This file is auto-generated from ChromeOS EC Open Source code from
+ * https://chromium.googlesource.com/chromiumos/platform/ec/+/master/include/ec_commands.h
  */
 
 /* Host communication command constants for Chrome EC */
EOF

# Change header guards
sed -i "s/__CROS_EC_EC_COMMANDS_H/__CROS_EC_COMMANDS_H/" "${tmp}"

# Remove non kernel code to prevent checkpatch warnings and simplify the .h.
unifdef -x2 -m -UCONFIG_HOSTCMD_ALIGNED -U__ACPI__ -D__KERNEL__ -U__cplusplus \
  -UCHROMIUM_EC "${tmp}"

# Check kernel checkpatch passes.
"${CROS_WORKON_SRCROOT}/src/repohooks/checkpatch.pl" -f "${tmp}"

cp "${tmp}" "${out}"
