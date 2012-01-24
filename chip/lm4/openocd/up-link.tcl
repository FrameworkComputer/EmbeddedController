# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Upload link image to flash and run it

reset halt
# Link proto0 has 128KB flash; proto1+ will likely have 256KB, in which
# case this'll need changing.
flash erase_sector 0 0 127
flash write_image ../../../build/link/ec.bin 0
reset
