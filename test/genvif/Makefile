# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CC=gcc
CFLAGS=-O2 -Isrc -I../../include
CFLAGS+= -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

CFLAGS+= -I. -DTEST_BUILD -DBOARD_TASKFILE=ec.tasklist
CFLAGS+= -DTEST_TASKFILE=genvif_test.tasklist

genvif: ../../util/genvif.c src/helper.c
	@$(CC) -o $@ $^ $(CFLAGS)

outdir := vif_output

$(outdir):
	mkdir vif_output

# Generate a VIF based on the test config without overrides.
$(outdir)/test_vif.xml: vif/$(notdir $@) genvif | $(outdir)
	./genvif -b test -o $(outdir)

# Generate expected output for a VIF without overrides by applying overrides for
# every field with expected values.
$(outdir)/exp_test_vif.xml: vif/$(notdir $@) genvif | $(outdir)
	./genvif -b exp_test -o $(outdir) -n -v vif/$(notdir $@)

# Generate a VIF based on the test config with overrides.
$(outdir)/over_test_vif.xml: vif/$(notdir $@) genvif | $(outdir)
	./genvif -b test_over -o $(outdir) -v vif/$(notdir $@)

# Generate expected output for a VIF with overrides by applying overrides for
# every field with expected values.
$(outdir)/exp_test_over_vif.xml: vif/$(notdir $@) genvif | $(outdir)
	./genvif -b exp_test_over -o $(outdir) -n -v vif/$(notdir $@)

.PHONY: clean test test_over

# Check that VIF without overrides matches expected output.
test: genvif $(outdir)/test_vif.xml $(outdir)/exp_test_vif.xml
	diff $(outdir)/test_vif.xml $(outdir)/exp_test_vif.xml

# Check that VIF with overrides matches expected output.
test_over: genvif $(outdir)/over_test_vif.xml $(outdir)/exp_test_over_vif.xml
	diff $(outdir)/test_over_vif.xml $(outdir)/exp_test_over_vif.xml

clean:
	rm -f genvif
	rm -rf vif_output
