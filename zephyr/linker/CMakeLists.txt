# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add the fixed sections to the output image.
zephyr_linker_sources(ROM_START SORT_KEY 1 fixed-sections.ld)
