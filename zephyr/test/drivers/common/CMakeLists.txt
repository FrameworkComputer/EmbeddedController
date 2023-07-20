# Common sources
target_sources(app PRIVATE
  src/main.c
  src/pm_handlers.c
  src/stubs.c
  src/test_mocks.c
  src/test_rules.c
  src/utils.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PLATFORM_EC}/driver/ppc/
    ${PLATFORM_EC}/zephyr/shim/src/led_driver
)
