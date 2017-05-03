#!/bin/bash
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

FLAGS_timeout=600
IMG=${1:-tigertail.bin}

echo "Flashing ${IMG}"

error() {
	printf "%s\n" "$*" >&2
}

die() {
	[[ "$#*" == "0" ]] || error "$@"
	exit 1
}

flash_stm32_dfu() {
	local DFU_DEVICE=0483:df11
	local ADDR=0x08000000

	[[ -e "${IMG}" ]] || die "File ${IMG} not found!"

	# Check for a suitable local dfu-util
	local LOCAL_DFU_UTIL=$(which dfu-util)
	if [[ -n "${LOCAL_DFU_UTIL}" ]]; then
		DFU_VERSION=$("${LOCAL_DFU_UTIL}" -V | head -n1 | cut -d' ' -f2)
		if [[ "${DFU_VERSION}" < "0.7" ]]; then
			LOCAL_DFU_UTIL=""
		fi
	fi
	local DFU_UTIL=${LOCAL_DFU_UTIL:-'./dfu-util'}

	which "${DFU_UTIL}" &> /dev/null || die \
		"no dfu-util util found.  Did you 'sudo emerge dfu-util'."

	local dev_cnt=$(lsusb -d "${DFU_DEVICE}" | wc -l)
	if [ $dev_cnt -eq 0 ] ; then
		die "unable to locate dfu device at ${DFU_DEVICE}."
	elif [ $dev_cnt -ne 1 ] ; then
		die "too many dfu devices (${dev_cnt}). Disconnect all but one."
	fi

	local SIZE=$(wc -c "${IMG}" | cut -d' ' -f1)
	# Remove read protection.
	sudo timeout -k 10 -s 9 "${FLAGS_timeout}" \
		${DFU_UTIL} -a 0 -s ${ADDR}:${SIZE}:force:unprotect -D "${IMG}"
	# Wait for mass-erase and reboot after unprotection.
	sleep 1
	# Actual image flashing.
	sudo timeout -k 10 -s 9 "${FLAGS_timeout}" \
		$DFU_UTIL -a 0 -s ${ADDR}:${SIZE} -D "${IMG}"
}

flash_stm32_dfu
