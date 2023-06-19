/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/tcpci.h"
#include "hooks.h"
#include "tcpm/tcpm.h"
#include "test/drivers/stubs.h"

void tcpc_un_init(void)
{
	tcpc_config[USBC_PORT_C0].irq_gpio.port->state->initialized = 0;
}
DECLARE_HOOK(HOOK_INIT, tcpc_un_init, HOOK_PRIO_INIT_I2C);
