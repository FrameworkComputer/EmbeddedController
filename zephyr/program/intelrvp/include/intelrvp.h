/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __INTELRVP_BOARD_H
#define __INTELRVP_BOARD_H

#include "compiler.h"
#include "gpio_signal.h"
#include "stdbool.h"

/* RVP ID read retry count */
#define RVP_VERSION_READ_RETRY_CNT 2

#define DC_JACK_MAX_VOLTAGE_MV 19000

FORWARD_DECLARE_ENUM(tcpc_rp_value);

/* TCPC AIC Config for MECC1.0 */
struct mecc_1_0_tcpc_aic_gpio_config_t {
	/* TCPC interrupt */
	enum gpio_signal tcpc_alert;
	/* PPC interrupt */
	enum gpio_signal ppc_alert;
	/* PPC interrupt handler */
	void (*ppc_intr_handler)(int port);
};
extern const struct mecc_1_0_tcpc_aic_gpio_config_t mecc_1_0_tcpc_aic_gpios[];

/* TCPC AIC Config for MECC1.1 */
struct mecc_1_1_tcpc_aic_gpio_config_t {
	/* TCPC interrupt */
	enum gpio_signal tcpc_alert;
};
extern const struct mecc_1_1_tcpc_aic_gpio_config_t mecc_1_1_tcpc_aic_gpios[];

void board_charging_enable(int port, int enable);
void board_vbus_enable(int port, int enable);
void board_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp);
void board_dc_jack_interrupt(enum gpio_signal signal);
bool is_typec_port(int port);
#endif /* __INTELRVP_BOARD_H */
