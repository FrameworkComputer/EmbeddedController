/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __INTELRVP_BOARD_H
#define __INTELRVP_BOARD_H

#include "compiler.h"
#include "gpio_signal.h"
#include "stdbool.h"

/* RVP ID read retry count */
#define RVP_VERSION_READ_RETRY_CNT	2

#define DC_JACK_MAX_VOLTAGE_MV 19000

FORWARD_DECLARE_ENUM(tcpc_rp_value);

struct tcpc_aic_gpio_config_t {
	/* TCPC interrupt */
	enum gpio_signal tcpc_alert;
	/* PPC interrupt */
	enum gpio_signal ppc_alert;
	/* PPC interrupt handler */
	void (*ppc_intr_handler)(int port);
};
extern const struct tcpc_aic_gpio_config_t tcpc_aic_gpios[];

void board_charging_enable(int port, int enable);
void board_vbus_enable(int port, int enable);
void board_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp);
void board_dc_jack_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);
bool is_typec_port(int port);
#endif /* __INTELRVP_BOARD_H */
