#!/bin/bash
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

# Make sure we are in the correct dir.
cd "$( dirname "${BASH_SOURCE[0]}" )" || exit

# Clean and previous cruft.
rm -rf build

DEST=build/tigertool
DATE=$(date +"%Y%m%d")

mkdir -p "${DEST}"
cp ../usb_serial/console.py "${DEST}"
cp ../../../../../chroot/usr/bin/dfu-util "${DEST}"
cp flash_dfu.sh "${DEST}"
cp tigertool.py "${DEST}"
cp tigertest.py "${DEST}"
cp README.md "${DEST}"

cp -r ecusb "${DEST}"
# Not compatible with glinux as of 4/28/2022.
# cp -r ../../../../../chroot/usr/lib64/python3.6/site-packages/usb "${DEST}"
find "${DEST}" -name "*.py[co]" -delete
cp -r ../usb_serial "${DEST}"

(cd build && tar -czf tigertool_"${DATE}".tgz tigertool)

echo "Done packaging tigertool_${DATE}.tgz"
