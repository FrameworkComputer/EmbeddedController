# SPDX-License-Identifier: Apache-2.0

add_library(rollback_initial OBJECT rollback_initial_data.c)

target_link_libraries(rollback_initial PUBLIC zephyr_interface)
target_link_libraries(rollback_initial PUBLIC cros_ec_interface)

# Add 'common' directory to include directories for rollback_private.h
target_include_directories(rollback_initial PUBLIC ${PLATFORM_EC}/common)

# The target below extract content of 'rollback_initial_data' structure from
# rollback_initial object and saves it in rollback_initial_data.bin.
# The structure is placed in separate section '.data.rollback_initial_data'
# because it's compiled with '-fdata-sections' option (enabled by default in
# Zephyr).
add_custom_target(initial_rollback_binary ALL
  COMMAND ${CMAKE_OBJCOPY} -Obinary --only-section .data.rollback_initial_data
	$<TARGET_OBJECTS:rollback_initial>
	${CMAKE_BINARY_DIR}/rollback_initial_data.bin
  COMMENT "Generating binary with initial rollback data"
  DEPENDS rollback_initial
  VERBATIM
)

add_custom_target(rw_rbver_binary ALL
  COMMAND ${CMAKE_OBJCOPY} -Obinary --only-section .rw_rbver
	$<TARGET_OBJECTS:rollback_initial>
	${CMAKE_BINARY_DIR}/rw_rbver.bin
  COMMENT "Generating RW_RBVER section content"
  DEPENDS rollback_initial
  VERBATIM
)
