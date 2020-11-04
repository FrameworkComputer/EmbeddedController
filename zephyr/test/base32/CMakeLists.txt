# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD native_posix)
project(base32)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

target_sources(app PRIVATE ${PLATFORM_EC}/test/base32.c)
