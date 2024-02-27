# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CC ?= gcc
PROGRAM := rma_reset
SOURCE  := $(PROGRAM).c
OBJS    := curve25519.o curve25519-generic.o sha256.o base32.o
LIBS    :=
LFLAGS  :=
CFLAGS  := -std=gnu99 \
        -Wall \
        -Werror \
        -Wpointer-arith \
        -Wcast-align \
        -Wcast-qual \
        -Wundef \
        -Wsign-compare \
        -Wredundant-decls \
        -Wmissing-declarations

ifneq ($(DEBUG),)
CFLAGS += -g -O0
else
CFLAGS += -O3
endif
#
# Add libusb-1.0 required flags
#
INCLUDE=-I. -I../../ -I../../fuzz -I../../test -I../../include -I../../chip/host
LIBS    += -lcrypto -lssl
CFLAGS  += ${INCLUDE}
CFLAGS  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
STANDALONE_FLAGS=${INCLUDE} -ffreestanding -fno-builtin \
                        -Ibuiltin/ -D"__keep= "

$(PROGRAM): $(SOURCE) $(OBJS) Makefile
	$(CC) $(CFLAGS) $(SOURCE) $(LFLAGS) $(LIBS) $(OBJS) -o $@

curve25519-generic.o: ../../common/curve25519-generic.c
	$(CC) $(STANDALONE_FLAGS) -c -o curve25519-generic.o \
				../../common/curve25519-generic.c

curve25519.o: ../../common/curve25519.c
	$(CC) $(STANDALONE_FLAGS) -c -o curve25519.o ../../common/curve25519.c

sha256.o: ../../common/sha256.c
	$(CC) $(STANDALONE_FLAGS) -c -o sha256.o ../../common/sha256.c

base32.o: ../../common/base32.c
	$(CC) $(STANDALONE_FLAGS) -c -o base32.o ../../common/base32.c

.PHONY: clean

clean:
	rm -rf *.o $(PROGRAM) *~
