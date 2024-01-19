/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "anraggar.h"
#include "ap_power/ap_power_events.h"
#include "charge_manager.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "keyboard_protocol.h"
#include "nissa_hdmi.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "typec_control.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

ZTEST_SUITE(anraggar, NULL, NULL, NULL, NULL, NULL);
