# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Utility functions for flash related test
#

import random

# Fixed random seed.
random.seed(1234)

def get_flash_size(helper):
    helper.ec_command("hcflashinfo")
    return int(helper.wait_output("flash_size = (?P<f>\d+)", use_re=True)["f"])

def get_ro_size(helper):
    helper.ec_command("rosize")
    return int(helper.wait_output(
        "RO image size = (?P<ro>0x[0-9a-f]+)", use_re=True)["ro"], 16)

def xor_sum(size, seed, mult, add):
    ret = 0
    for i in xrange(size):
        ret ^= (seed & 0xff)
        seed = seed * mult + add
    return ret

def test_erase(helper, offset, size):
    helper.ec_command("hcflasherase %d %d" % (offset, size))
    helper.wait_output("Flash erase at %x size %x" % (offset, size))

def test_read(helper, offset, size):
    helper.ec_command("hcflashread %d %d" % (offset, size))
    while size > 0:
        cur_size = size if size <= 32 else 32
        expect_str = ''.join([("%02x" % (x & 0xff)) for x in
                range(offset, offset + cur_size)])
        helper.wait_output(expect_str)
        offset = offset + cur_size
        size = size - cur_size

def test_write(helper, offset, size, expect_fail=False):
    seed = random.randint(2, 10000)
    mult = random.randint(2, 10000)
    add  = random.randint(2, 10000)
    helper.ec_command("hcflashwrite %d %d %d %d %d" %
                      (offset, size, seed, mult, add))
    if expect_fail:
        helper.wait_output("Command returned error")
    else:
        expected_sum = xor_sum(size, seed, mult, add)
        helper.wait_output("Flash write at %x size %x XOR %x" %
                           (offset, size, expected_sum))
