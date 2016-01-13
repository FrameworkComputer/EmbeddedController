/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_REVS_H
#define __CROS_EC_BOARD_REVS_H

#define OAK_REV0 0
#define OAK_REV1 1
#define OAK_REV2 2
#define OAK_REV3 3
#define OAK_REV4 4
#define OAK_REV5 5
#define OAK_REV_LAST    OAK_REV5
#define OAK_REV_DEFAULT OAK_REV5

#if !defined(BOARD_REV)
#define BOARD_REV OAK_REV_DEFAULT
#endif

#if BOARD_REV < OAK_REV1 || BOARD_REV > OAK_REV_LAST
#error "Board revision out of range"
#endif

#endif /* __CROS_EC_BOARD_REVS_H */
