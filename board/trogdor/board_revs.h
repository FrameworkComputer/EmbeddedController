/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_REVS_H
#define __CROS_EC_BOARD_REVS_H

#define TROGDOR_REV0 0
#define TROGDOR_REV1 1
#define TROGDOR_REV_LAST    TROGDOR_REV1

#define TROGDOR_REV_DEFAULT TROGDOR_REV0

#if !defined(BOARD_REV)
#define BOARD_REV TROGDOR_REV_DEFAULT
#endif

#if BOARD_REV < TROGDOR_REV0 || BOARD_REV > TROGDOR_REV_LAST
#error "Board revision out of range"
#endif

#endif /* __CROS_EC_BOARD_REVS_H */
