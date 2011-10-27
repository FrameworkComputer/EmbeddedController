/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * board.h -- defines the board interface. The implementation will be under
 *            board/your_board_name/main.c.
 */

#ifndef __BOARD_BOARD_INTERFACE_H
#define __BOARD_BOARD_INTERFACE_H

#include "cros_ec/include/ec_common.h"

/* The initialize function of a board. Called by the EC core at start.
 * The main mission is to register its callback function to the EC core.
 */
EcError BoardInit();


#endif  /* __BOARD_BOARD_INTERFACE_H */
