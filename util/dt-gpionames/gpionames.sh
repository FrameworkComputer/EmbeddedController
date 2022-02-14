#!/bin/bash
#
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [ $# != 1 ]; then
	echo "Usage $0 <project>"
	exit
fi

basedir=$(pwd)
dtprog="${basedir}/dt-gpionames"
proj=$1
dtb="/tmp/dt-${proj}.dtb"
names="/tmp/gpionames-${proj}.dts"
dts="../../build/zephyr/${proj}/build-ro/zephyr/zephyr.dts"

# Remove dt-gpionames binary
trap 'rm -f ${dtprog}' EXIT

proj=$1
echo "Using ${proj} project"
echo "Building ${dtprog}"
go build
echo "Building project ${proj}"
(cd ../../zephyr; zmake configure -b "${proj}")
echo "Compiling device tree ${dts} to ${dtb}"
dtc --out "${dtb}" "${dts}"
echo "Generating ${names} from ${dtb}"
${dtprog} --output "${names}" --input "${dtb}"
echo "Successfully generated ${names}"
