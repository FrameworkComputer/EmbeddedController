# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

CC=gcc
CFLAGS=-O2 -Isrc -I../../include
CFLAGS+= -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

genvif: ../../util/genvif.c src/helper.c
	@$(CC) -o $@ $^ $(CFLAGS)

test: genvif
	@mkdir vif_output 2>/dev/null
	@./genvif -b test -o vif_output
	@./genvif -b exp_test -o vif_output -n -v vif/exp_test_vif.xml
	@diff vif_output/test_vif.xml vif_output/exp_test_vif.xml
	@./genvif -b test_over -o vif_output -v vif/over_test_vif.xml
	@./genvif -b exp_test_over -o vif_output -n -v vif/exp_test_over_vif.xml
	@diff vif_output/test_over_vif.xml vif_output/exp_test_over_vif.xml

.PHONY: clean
clean:
	@rm -f genvif
	@rm -rf vif_output
