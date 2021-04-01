/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MCHP-specific I2C module for Chrome EC */

#ifndef _I2C_CHIP_H
#define _I2C_CHIP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Place any C interfaces here */

/*
 * function returns the controller for I2C
 * if there is a special assignment, function board.c can override this.
 */
__override_proto int board_i2c_p2c(int port);

#ifdef __cplusplus
}
#endif

#endif /* _I2C_CHIP_H */
