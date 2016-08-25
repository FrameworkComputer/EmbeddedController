# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(V),)
Q := @
else
Q :=
endif

obj = ../../build/tpm_test
src = .
SWIG = /usr/bin/swig

vpath %c $(src) ../../chip/g/dcrypto $(src)/testlib

CFLAGS = -fPIC
CFLAGS += -I /usr/include/python2.7
CFLAGS += -I../../../../third_party/cryptoc/include
CFLAGS += -I../../chip/g/dcrypto
CFLAGS += -I.
CFLAGS += -Itestlib
CFLAGS += -DLIBFTDI1=1
CFLAGS += -c
CFLAGS += -DCR50_NO_BN_ASM
TARGET = ftdi_spi_tpm

.PRECIOUS: $(obj)/ftdi_spi_tpm_wrap.c

all: $(obj)/_$(TARGET).so $(obj)/bn_test

BN_OBJS = $(obj)/bn_test.o $(obj)/common.o $(obj)/bn.o

OBJS = $(obj)/$(TARGET).o $(obj)/$(TARGET)_wrap.o $(obj)/mpsse.o \
	$(obj)/support.o

DEPS := $(OBJS:.o=.o.d) $(BN_OBJS:.o=.o.d)

$(OBJS) $(BN_OBJS): | $(obj)

$(obj)/%.o: $(obj)/%.c
	@echo "  CC      $(notdir $@)"
	$(Q)gcc $(CFLAGS) -o $@ $<

$(obj)/%.o: %.c
	@echo "  CC      $(notdir $@)"
	$(Q)gcc $(CFLAGS) -Wall -Werror -MMD -MF $@.d -o $@ $<

$(obj)/_$(TARGET).so: $(OBJS) $(obj)/$(TARGET).py
	@echo "  LD      $(notdir $@)"
	$(Q)rm -f $@
	$(Q)gcc -shared $(OBJS) -lftdi1 -o $@

$(obj)/%_wrap.c: $(src)/%.i
	@echo "  SWIG    $(notdir $@)"
	$(Q)swig -python -outdir $(obj) -o $@ $<

clean:
	@rm -rf $(obj)/

$(obj):
	@echo "  MKDIR   $(obj)"
	$(Q)mkdir -p $(obj)

$(obj)/bn_test: $(BN_OBJS)
	@echo "  LD      $(notdir $@)"
	$(Q)$(CC) -o $@ $^ -lcrypto

-include $(DEPS)
