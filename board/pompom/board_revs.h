/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_REVS_H
#define __CROS_EC_BOARD_REVS_H

#define POMPOM_REV0 0
#define POMPOM_REV1 1
#define POMPOM_REV_LAST    POMPOM_REV1
#define POMPOM_REV_DEFAULT POMPOM_REV1

#if !defined(BOARD_REV)
#define BOARD_REV POMPOM_REV_DEFAULT
#endif

#if BOARD_REV < POMPOM_REV0 || BOARD_REV > POMPOM_REV_LAST
#error "Board revision out of range"
#endif


#endif /* __CROS_EC_BOARD_REVS_H */
