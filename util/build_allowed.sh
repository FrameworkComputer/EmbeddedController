#!/bin/bash
#
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Taken from U-Boot and modified.

# This grubby little script creates the list of allowed configurations. This
# file contains all the config options which are allowed to be used outside
# Kconfig. Please do not add things to the list. Instead, add your new option to
# Kconfig.
#
# Usage:
#   build_allowed.sh [-u]
#
#   -u : Update the existing allowed file

export LC_ALL=C LC_COLLATE=C

# Current list of allowed ad-hoc CONFIGs
allowed=util/config_allowed.txt

[ "$1" == "-u" ] && update=1

tmp=$(mktemp -d)
kconfigs="${tmp}/kconfigs"

#
# Look for the CONFIG options, excluding those in Kconfig and defconfig files.
#
git grep CONFIG_ | \
	grep -E -vi "(Kconfig:|defconfig:|README|\.py|\.pl:)" \
	| tr ' \t' '\n' \
	| sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\).*/\1/p' \
	| sort | uniq >"${tmp}/allowed.tmp1";

# We need a list of the valid Kconfig options to exclude these from the allowed
# list.
find . -type f -name "Kconfig*" -exec cat {} \; | sed -n -e \
	's/^\s*\(config\|menuconfig\) *\([A-Za-z0-9_]*\)$/CONFIG_\2/p' \
	| sort | uniq > "${kconfigs}"

# Most Kconfigs follow the pattern of CONFIG_PLATFORM_EC_*.  Strip PLATFORM_EC_
# from the config name to match the cros-ec namespace.
sed -e 's/^CONFIG_PLATFORM_EC_/CONFIG_/p' "${kconfigs}" | sort | uniq \
	> "${tmp}/allowed.tmp2"

# Use only the options that are present in the first file but not the second.
# These comprise new ad-hoc CONFIG options.
comm -23 "${tmp}/allowed.tmp1" "${tmp}/allowed.tmp2" \
	| sort | uniq >"${tmp}/allowed.tmp3"

# If ${allowed} already exists, take the intersection of the current
# list and the new one.  We do not want to increase the allowed options.
if [ -r "${allowed}" ]; then
	comm -12 "${tmp}/allowed.tmp3" "${allowed}" > "${tmp}/allowed.tmp4"

	# Find any ad-hoc configs that now have Kconfig options
	comm -13 "${tmp}/allowed.tmp4" "${allowed}" > "${tmp}/allowed.tmp5"
	if [ -n "${update}" ]; then
		echo >&2 "Removing these CONFIG options from the allowed list:"
		comm -13 "${tmp}/allowed.tmp4" "${allowed}"
		mv "${tmp}/allowed.tmp4" "${allowed}"
	elif [ -s "${tmp}/allowed.tmp5" ]; then
		echo >&2 "The following options are now in Kconfig:"
		cat >&2 "${tmp}/allowed.tmp5"
		echo >&2
		echo >&2 "Please run this to update the list of allowed ad-hoc"
		echo >&2 "CONFIGs and include this update in your CL:"
		echo >&2
		echo -e >&2 "\t./util/build_allowed.sh -u"
		exit 1
	fi
else
	# If there is no file yet, add one. This allows it to be regenerated
	# from scratch if needed.
	mv "${tmp}/allowed.tmp3" "${allowed}"
fi

rm -rf "${tmp}"

unset LC_ALL LC_COLLATE
