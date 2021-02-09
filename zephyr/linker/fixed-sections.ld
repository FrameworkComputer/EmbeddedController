/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_PLATFORM_EC) && !defined(CONFIG_ARCH_POSIX)
*(.fixed.*)
#endif
