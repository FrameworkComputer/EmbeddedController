/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STDNORETURN_H__
#define __CROS_EC_STDNORETURN_H__

/*
 * Only defined for C: https://en.cppreference.com/w/c/language/_Noreturn
 *
 * C++ uses [[noreturn]]: https://en.cppreference.com/w/cpp/language/attributes/noreturn
 */
#ifndef __cplusplus
#ifndef noreturn
#define noreturn _Noreturn
#endif
#endif

#endif /* __CROS_EC_STDNORETURN_H__ */
