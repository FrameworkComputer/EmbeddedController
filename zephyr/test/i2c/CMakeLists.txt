# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD native_posix)
project(base32)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Need to ensure that we are including only zephyr definitions in include files
# We cannot set these via kconfig, since this unit test does not bring in the
# zephyr-chrome repository
zephyr_compile_definitions("CONFIG_ZEPHYR")
zephyr_compile_definitions("CONFIG_PLATFORM_EC_I2C")

# We need to include the EC include directory and this local test directory
# for the task defines
zephyr_include_directories(
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/fuzz"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}/include")

# Include test file, test under test and console dependency
target_sources(app PRIVATE
	src/main.c
	"${PLATFORM_EC}/zephyr/shim/src/console.c"
	"${PLATFORM_EC}/zephyr/shim/src/i2c.c"
	"${PLATFORM_EC}/zephyr/shim/src/util.c"
	"${PLATFORM_EC}/common/i2c_master.c")
