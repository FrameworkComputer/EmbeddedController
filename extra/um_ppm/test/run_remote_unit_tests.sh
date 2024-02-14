#!/bin/bash
#
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

port=22
pub_key=~/.ssh/testing_rsa

if [ $# -lt 1 ]; then
echo "run_remote_unit_test.sh target [port]"
echo ""
echo "The script compiles remote unit tests, copies it to the target, loads"
echo "ucsi_um_test module and finally executes remote unit tests on the target."
echo ""
echo "Default port is 22."
exit
fi

target=$1
[ $# -gt 1 ] && port=$2

echo "Compile remote unit tests ..."
cros_sdk bash --login -c 'cd ../platform/ec/extra/um_ppm; make remote_tests'

if [ ! -f ~/.ssh/testing_rsa ]
then
	echo "Setting public key authentication ..."
	mkdir -p ~/.ssh
	curl -s https://chromium.googlesource.com/chromiumos/chromite/+/main/ssh_keys/testing_rsa?format=TEXT \
	| base64 --decode > "${pub_key}"
	chmod 0400 "${pub_key}"
fi

echo "Copy remote unit tests to target ${target} port ${port} ..."
scp -i "${pub_key}" -P "${port}" remote_unit_tests "${target}":~/

# load ucsi_um_test module
ssh -i "${pub_key}" -p "${port}" "${target}" "modprobe ucsi_um_test"

echo "Execute remote unit tests on target ${target} port ${port} ..."
ssh -i "${pub_key}" -p "${port}" "${target}" /root/remote_unit_tests
