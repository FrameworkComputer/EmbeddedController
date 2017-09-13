# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# V unset for normal output, V=1 for verbose output, V=0 for silent build
# (warnings/errors only). Use echo thus: $(call echo,"stuff  to  echo")
ifeq ($(V),0)
Q := @
echo = echo -n;
else
echo = echo $(1);
ifeq ($(V),)
Q := @
else
Q :=
endif
endif

obj = ../../build/tpm_test
src = .
SWIG = /usr/bin/swig

vpath %c $(src) ../../chip/g/dcrypto $(src)/testlib

CFLAGS = -fPIC
CFLAGS += -I /usr/include/python2.7
CFLAGS += -I../../../../third_party/cryptoc/include
CFLAGS += -I../../board/cr50
CFLAGS += -I../../chip/g
CFLAGS += -I../../chip/g/dcrypto
CFLAGS += -I../../include
CFLAGS += -I..
CFLAGS += -I../..
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
	$(call echo,"  CC      $(notdir $@)")
	$(Q)gcc $(CFLAGS) -o $@ $<

$(obj)/%.o: %.c
	$(call echo,"  CC      $(notdir $@)")
	$(Q)gcc $(CFLAGS) -Wall -Werror -MMD -MF $@.d -o $@ $<

$(obj)/_$(TARGET).so: $(OBJS) $(obj)/$(TARGET).py
	$(call echo,"  LD      $(notdir $@)")
	$(Q)rm -f $@
	$(Q)gcc -shared $(OBJS) -lftdi1 -o $@

$(obj)/%_wrap.c: $(src)/%.i
	$(call echo,"  SWIG    $(notdir $@)")
	$(Q)swig -python -outdir $(obj) -o $@ $<

clean:
	@rm -rf $(obj)/

$(obj):
	$(call echo,"  MKDIR   $(obj)")
	$(Q)mkdir -p $(obj)

$(obj)/bn_test: $(BN_OBJS)
	$(call echo,"  LD      $(notdir $@)")
	$(Q)$(CC) -o $@ $^ -lcrypto

-include $(DEPS)
