/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>

#include "cros_board_info.h"
#include "flash.h"
#include "pw_unit_test/framework.h"
#include "pw_unit_test/logging_event_handler.h"

int main() {
  struct cbi_header header = {
      .magic = {'C', 'B', 'I'},
      .crc = 0,
      .version = ((static_cast<uint16_t>(CBI_VERSION_MAJOR) & 0xff) << 8) |
                 (static_cast<uint16_t>(CBI_VERSION_MINOR) & 0xff),
      .total_size = sizeof(struct cbi_header),
  };
  header.crc = cbi_crc8(&header);
  crec_flash_physical_write(
      0x3f000, sizeof(header), reinterpret_cast<const char*>(&header));
  testing::InitGoogleTest(nullptr, nullptr);
  pw::unit_test::LoggingEventHandler handler;
  pw::unit_test::RegisterEventHandler(&handler);
  return RUN_ALL_TESTS();
}
