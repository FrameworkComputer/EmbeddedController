# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
CLANG:=clang-6.0
KLEE:=klee
KTEST_TOOL:=ktest-tool

.PHONY: report
report: klee-last
	$(KTEST_TOOL) \
		$(patsubst %.assert.err,%.ktest,$(wildcard klee-last/test*.assert.err))

klee-last: validate_port_power.bc
	$(KLEE) --emit-all-errors $^

validate_port_power.bc: validate_port_power.c port-sm.c
	$(CLANG) -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone -o $@ $<

.PHONY: clean
clean:
	rm -rf validate_port_power.bc klee-last klee-out-*
