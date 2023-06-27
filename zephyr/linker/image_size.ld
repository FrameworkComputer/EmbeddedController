/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_PLATFORM_EC_RW_TOTAL_IMAGE_SIZE)
__image_size = CONFIG_PLATFORM_EC_RW_TOTAL_IMAGE_SIZE;
#else
/*
 * Intentionally set to 0. Some components, such as EFS2, need this value.
 * Having it be 0 will make it easier to find and add new cores.
 */
__image_size = 0;
#endif
