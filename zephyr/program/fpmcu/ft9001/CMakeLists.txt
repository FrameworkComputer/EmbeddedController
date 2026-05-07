# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(DEFINED CONFIG_CROS_EC_RO)

add_library(config_page OBJECT src/config_page.c)
set_source_files_properties(src/config_page.c PROPERTIES
  COMPILE_OPTIONS $<TARGET_PROPERTY:compiler,prohibit_lto>
)

target_link_libraries(config_page PUBLIC zephyr_interface)
target_link_libraries(config_page PUBLIC cros_ec_interface)

add_custom_target(config_page_binary ALL
  COMMAND ${CMAKE_OBJCOPY} -Obinary --only-section .data.config_page
	$<TARGET_OBJECTS:config_page>
	${CMAKE_BINARY_DIR}/config_page.bin
  COMMENT "Generating binary with configuration page"
  DEPENDS config_page
  VERBATIM
)

endif()

# TODO(b/502744651): Remove OTP initialization once all boards are initialized
if(DEFINED CONFIG_CROS_EC_RW AND DEFINED CONFIG_FOCALTECH_OTP_KEY)
  zephyr_library_sources("src/otp_init.c")
endif()
