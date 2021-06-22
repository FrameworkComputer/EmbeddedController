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
 * Function returns the controller for I2C.
 *
 * Default function assigns controller for I2C port with modulo operation. If
 * the I2C ports used are greater than MCHP_I2C_CTRL_MAX, then I2C ports will
 * share the controller. Typically Type-C chips need individual controller per
 * port because of heavy I2C transactions. Hence, define a board specific
 * controller assignment when the I2C ports used are greater than
 * MCHP_I2C_CTRL_MAX.
 */
__override_proto int board_i2c_p2c(int port);

#ifdef __cplusplus
}
#endif

#endif /* _I2C_CHIP_H */
