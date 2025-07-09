# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources(app PRIVATE
  src/panic_output_crash_cmd.c
  src/panic_output_get_panic_data_write.c
  src/panic_output_print.c
)
