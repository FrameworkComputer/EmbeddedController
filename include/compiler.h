/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_COMPILER_H
#define __CROS_EC_COMPILER_H

/*
 * See https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
 */
#define GCC_VERSION \
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#endif /* __CROS_EC_COMPILER_H */
