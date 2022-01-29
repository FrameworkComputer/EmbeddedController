#!/bin/bash
#
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Taken from U-Boot and modified.
#
# Check that the .config file provided does not introduce any new ad-hoc CONFIG
# options
#
# Use util/build_allowed.sh to generate the list of current ad-hoc
# CONFIG options (those which are not in Kconfig).

# Usage
#    check_allowed.sh <path to .config> <path to allow file> <source dir>
#
# For example:
#   scripts/check_allowed.sh build/volteer/.config config_allowed.txt .

set -e
set -u

PROG_NAME="${0##*/}"

usage() {
	echo >&2 "Check that a build does not introduce new ad-hoc CONFIGs"
	echo >&2 "Usage:"
	echo -e >&2 "\t${PROG_NAME} <.config file> <allow file> <source dir>"
	exit 1
}

die() {
	echo >&2 "$1"
	exit 2
}

[ $# -ge 3 ] || usage

config="$1"
allow="$2"
srctree="$3"

tmp=$(mktemp -d)

# Temporary files
new_configs="${tmp}/configs"
suspects="${tmp}/suspects"
kconfigs="${tmp}/kconfigs"
ok="${tmp}/ok"
new_adhoc="${tmp}/adhoc"

export LC_ALL=C LC_COLLATE=C

# Get a sorted list of CONFIG options in the .config file
sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\).*/\1/p' "${config}" | sort | uniq \
	>"${new_configs}"

# Find any not mentioned in the allowed file
comm -23 --check-order "${new_configs}" "${allow}" > "${suspects}" || \
	die "${allow} must be sorted"

# Find all the Kconfig options so far defined
find "${srctree}" -type f -name "Kconfig*" -exec cat {} \; | sed -n -e \
	's/^\s*\(config\|menuconfig\) *\([A-Za-z0-9_]*\)$/CONFIG_\2/p' \
	| sort | uniq > "${kconfigs}"

# Most Kconfigs follow the pattern of CONFIG_PLATFORM_EC_*.  Strip PLATFORM_EC_
# from the config name to match the cros-ec namespace.
sed -e 's/^CONFIG_PLATFORM_EC_/CONFIG_/p' "${kconfigs}" | sort | uniq > "${ok}"

# Complain about any new ad-hoc CONFIGs
comm -23 "${suspects}" "${ok}" >"${new_adhoc}"
if [ -s "${new_adhoc}" ]; then
	echo >&2 "Error: The EC is in the process of migrating to Zephyr."
	echo -e >&2 "\tZephyr uses Kconfig for configuration rather than"
	echo -e >&2 "\tad-hoc #defines."
	echo -e >&2 "\tAny new EC CONFIG options must ALSO be added to Zephyr"
	echo -e >&2 "\tso that new functionality is available in Zephyr also."
	echo -e >&2 "\tThe following new ad-hoc CONFIG options were detected:"
	echo >&2
	cat >&2 "${new_adhoc}"
	echo >&2
	echo >&2 "Please add these via Kconfig instead. Find a suitable Kconfig"
	echo >&2 "file in zephyr/ and add a 'config' or 'menuconfig' option."
	echo >&2 "Also see details in http://issuetracker.google.com/181253613"
	echo >&2
	echo >&2 "To temporarily disable this, use: ALLOW_CONFIG=1 make ..."
	rm -rf "${tmp}"
	exit 1
else
	# If we are running in a git repo, check if we can remove some things
	# from the allowed file
	if git status 2>/dev/null; then
		./util/build_allowed.sh
	fi
fi

rm -rf "${tmp}"
