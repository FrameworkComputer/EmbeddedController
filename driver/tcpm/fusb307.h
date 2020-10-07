/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */
/* For Fairchild FUSB307 */
#ifndef __CROS_EC_FUSB307_H
#define __CROS_EC_FUSB307_H

#include "usb_pd.h"

#define FUSB307_I2C_SLAVE_ADDR_FLAGS 0x50

#define TCPC_REG_RESET		0xA2
#define TCPC_REG_RESET_PD_RESET	BIT(1)
#define TCPC_REG_RESET_SW_RESET	BIT(0)

#define TCPC_REG_GPIO1_CFG		0xA4
#define TCPC_REG_GPIO1_CFG_GPO1_VAL	BIT(2)
#define TCPC_REG_GPIO1_CFG_GPI1_EN	BIT(1)
#define TCPC_REG_GPIO1_CFG_GPO1_EN	BIT(0)

int fusb307_power_supply_reset(int port);

extern const struct tcpm_drv fusb307_tcpm_drv;

#endif /* __CROS_EC_FUSB307_H */
