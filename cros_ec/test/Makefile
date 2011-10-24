# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

INCLUDES += -I./include \
		-I$(CROS_EC_DIR)/lib/include
BUILD_ROOT = ${BUILD}/cros_ec/test

TEST_NAMES = ec_os_test

TEST_BINS = $(addprefix ${BUILD_ROOT}/,$(TEST_NAMES))

# TODO: port over test lib from vboot_reference
# TEST_LIB = ${BUILD_ROOT}/test.a
# TEST_LIB_SRCS = test_common.c timer_utils.c
# TEST_LIB_OBJS = $(TEST_LIB_SRCS:%.c=${BUILD_ROOT}/%.o)
# ALL_DEPS = $(addsuffix .d,${TEST_BINS} ${TEST_LIB_OBJS})

# Allow multiple definitions, so tests can mock functions from other libraries
CFLAGS +=  -MMD -MF $@.d -Xlinker --allow-multiple-definition

LIBS := ${TEST_LIB} $(CROS_EC_LIB) $(CHIP_STUB_LIB)

ifneq (${RUNTESTS},)
EXTRA_TARGET = runtests
endif

all: $(TEST_BINS) ${EXTRA_TARGET}

# ${TEST_LIB}: ${TEST_LIB_OBJS}
#	rm -f $@
#	ar qc $@ $^

${BUILD_ROOT}/%.o : %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MF $@.d -c -o $@ $<

${BUILD_ROOT}/% : %.c ${LIBS}
	$(CC) $(CFLAGS) $(INCLUDES) $< ${LIBS} -o $@ -lrt

ALLTESTS = ec_os_test

runtests:
	${BUILD_ROOT}/ec_os_test

-include ${ALL_DEPS}
