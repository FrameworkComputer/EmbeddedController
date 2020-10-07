/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific hwtimer module for Chrome EC */

#ifndef __CROS_EC_LPC_CHIP_H
#define __CROS_EC_LPC_CHIP_H

/* For host registers initialization via SIB module */
void host_register_init(void);

/* eSPI Initialization functions */
void espi_init(void);
/* eSPI reset assert/de-assert interrupt */
void espi_espirst_handler(void);
/* LPC PLTRST assert/de-assert interrupt */
void lpc_lreset_pltrst_handler(void);
#endif /* __CROS_EC_LPC_CHIP_H */
