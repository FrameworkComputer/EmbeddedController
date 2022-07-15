/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SYS_TYPES_H__
#define __CROS_EC_SYS_TYPES_H__

/* Data type for POSIX style clock() implementation */
typedef long clock_t;

/* There is a GCC macro for a size_t type, but not for a ssize_t type.
 * The following construct convinces GCC to make __SIZE_TYPE__ signed.
 */
#define unsigned signed
typedef __SIZE_TYPE__ ssize_t;
#undef unsigned

#endif /* __CROS_EC_SYS_TYPES_H__ */
