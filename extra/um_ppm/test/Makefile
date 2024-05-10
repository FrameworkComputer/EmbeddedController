# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PARENT_DIR:=$(dir $(abspath $(firstword $(MAKEFILE_LIST))/..))

# Make sure to compile with C++
CXX=clang++
CXXFLAGS=-std=c++17 -Wall -I/usr/include/gtest -I$(PARENT_DIR)
CFLAGS=-Wall -Werror -I$(PARENT_DIR)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.co: ../%.c
	$(CC) -c -o $@ $< $(CFLAGS)

run_local_tests: unit_tests
	./unit_tests

unit_tests: ppm_tests.o ppm_common.co platform_usermode.co
	$(CXX) -o $@ $^ $(CXXFLAGS) -lgtest -lgtest_main

remote_unit_tests: opm_tests.cc ppm_common.co platform_usermode.co \
	um_ppm_chardev.co
	$(CXX) -o $@ $^ $(CXXFLAGS) -lgmock -lgtest -lgtest_main

.PHONY: clean

clean:
	rm -f *.o *.co unit_tests remote_unit_tests
