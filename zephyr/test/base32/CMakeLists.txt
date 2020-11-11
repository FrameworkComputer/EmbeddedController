# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD native_posix)
project(base32)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Need to ensure that we are including only zephyr definitions in include files
# We cannot set these via kconfig, since this unit test does not bring in the
# zephyr-chrome repository
zephyr_compile_definitions("CONFIG_ZEPHYR")

# We need to include the EC include directory and this local test directory
# for the task defines
zephyr_include_directories(
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/fuzz"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}/include")

# Include test file, test under test and console dependency
target_sources(app PRIVATE "${PLATFORM_EC}/test/base32.c")
target_sources(app PRIVATE "${PLATFORM_EC}/common/base32.c")
