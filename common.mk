# Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ALL_OBJS = $(ALL_SRCS:%.c=${BUILD_ROOT}/%.o)
ALL_DEPS = $(ALL_OBJS:%.o=%.o.d)

#
# For this target (all) to be built by default, the including file must not
# define any other targets above the line including this file.
#
# This all: rule must be above the %.o: %.c rule below, otherwise the
# rule below becomes the default target.
#
all: ${ALL_OBJS}

${BUILD_ROOT}/%.o : %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MF $@.d -c -o $@ $<

-include ${ALL_DEPS}
