/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_PLATFORM_EC) && !defined(CONFIG_ARCH_POSIX)
/*
 * Put the current_image_data (.fixed.ver) at the beginning so that it won't
 * be displaced when other data are added.
 */
*(.fixed.ver)
*(.fixed.*)
#endif
