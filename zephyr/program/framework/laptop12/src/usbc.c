/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cypress_pd_common.h"

/*
 * Override cypd_evaluate_port_profile() for single 3A port policy
 * Note: To meet this feature, the PD firmware needs to set all ports' Rp value to 1.5A.
 */
__override void cypd_evaluate_port_profile(int controller, int port, int ccg_event)
{
	int pd_port = PDPORT(controller, port);
	static uint8_t port_pdo_is_changed;

#ifndef CONFIG_SELECT_3A_TYPEC_OUTPUT_CURRENT
	return
#endif

	/* Skip to evaluate the port profile if the PD chip only one type-c port */
	if (pd_chip_config[controller].support_max_port < 2)
		return;

	/* Skip to evaluate the port profile if the safety level is 2 or more */
	if (cypd_get_safety_level() >= TYPEC_SAFETY_LEVEL_2)
		return;

	/**
	 * If the unit acts as a power source, the PD controller must limit the output
	 * current according to the maximum operating current. Therefore, we only need
	 * to detect the first 3A-capable port and re-negotiate the remaining USB-C ports
	 * down to 1.5A.
	 */
	if (pd_port_states[pd_port].max_operating_current > 1500 &&
	    (pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] ==
	    CCG_PD_CMD_SET_TYPEC_3A) &&
		ccg_event != CCG_RESPONSE_PORT_DISCONNECT) {

		/* Re-negotiate the PDOs of other USB-C ports down to 1.5A */
		for (int p = 0; p < PD_PORT_COUNT; p++) {
			/* Detect first 3A device, not need to change the PDO */
			if (p == pd_port) {
				continue;
			}

			/**
			 * USB-C connects a PD device, re-negotiate the PDO down to 1.5A.
			 * Don't need to re-negotiate the PDO if the USB-C connects a source
			 * device or disconnect
			 */
			if ((pd_port_states[p].c_state == CCG_STATUS_SINK &&
				pd_port_states[p].max_operating_current != 0) ||
				pd_port_states[p].c_state == CCG_STATUS_NOTHING) {

				cypd_select_pdo(PORT_TO_CONTROLLER(p), PORT_TO_CONTROLLER_PORT(p),
					CCG_PD_CMD_SET_TYPEC_1_5A);
				pd_port_states[p].safety_table[TYPEC_SAFETY_LEVEL_0] =
					CCG_PD_CMD_SET_TYPEC_1_5A;
				pd_port_states[p].safety_table[TYPEC_SAFETY_LEVEL_1] =
					CCG_PD_CMD_SET_TYPEC_1_5A;
				/* Wait for PD re-negotiation to complete */
				k_msleep(100);

				port_pdo_is_changed |= BIT(p);
			}
		}
	}

	if (ccg_event == CCG_RESPONSE_PORT_DISCONNECT) {
		bool any_sink_port_connected = false;

		/* Restore the non-PD device's PDO */
		if (pd_port_states[pd_port].c_state == CCG_STATUS_SINK &&
			pd_port_states[pd_port].max_operating_current == 0) {
			cypd_select_pdo(PORT_TO_CONTROLLER(pd_port),
				PORT_TO_CONTROLLER_PORT(pd_port),
				pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0]);
		}

		for (int p = 0; p < PD_PORT_COUNT; p++) {
			if ((p != pd_port) && (pd_port_states[p].c_state == CCG_STATUS_SINK)) {
				any_sink_port_connected = true;
				break;
			}
		}

		/* If pdo is no change, end to evaluate */
		if (!port_pdo_is_changed) {
			return;
		}

		if (!any_sink_port_connected) {
			/* Restore the PDO to 3A once all USB-C ports are disconnected */
			for (int p = 0; p < PD_PORT_COUNT; p++) {

				if (port_pdo_is_changed & BIT(p)) {
					cypd_select_pdo(PORT_TO_CONTROLLER(p),
						PORT_TO_CONTROLLER_PORT(p),
						CCG_PD_CMD_SET_TYPEC_3A);
					pd_port_states[p].safety_table[TYPEC_SAFETY_LEVEL_0] =
						CCG_PD_CMD_SET_TYPEC_3A;
					pd_port_states[p].safety_table[TYPEC_SAFETY_LEVEL_1] =
						CCG_PD_CMD_SET_TYPEC_3A;
					/* Wait for PD re-negotiation to complete */
					k_msleep(100);
				}
			}
			port_pdo_is_changed = 0;
		} else {
			/**
			 * When a port is disconnected while other ports remain active:
			 * If another port is currently drawing 3A (high current), we must
			 * downgrade this disconnected port's default output to 1.5A.
			 * This ensures the total power budget is not exceeded when a new
			 * device is plugged into this port later.
			 */
			bool other_port_is_3A = false;

			for (int p = 0; p < PD_PORT_COUNT; p++) {
				if (p != pd_port && pd_port_states[p].max_operating_current > 1500
				    && pd_port_states[p].safety_table[TYPEC_SAFETY_LEVEL_0] ==
				    CCG_PD_CMD_SET_TYPEC_3A) {
					other_port_is_3A = true;
					break;
				}
			}

			if (other_port_is_3A &&
			    pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] !=
			    CCG_PD_CMD_SET_TYPEC_1_5A) {
				cypd_select_pdo(PORT_TO_CONTROLLER(pd_port),
					PORT_TO_CONTROLLER_PORT(pd_port),
					CCG_PD_CMD_SET_TYPEC_1_5A);
				pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] =
					CCG_PD_CMD_SET_TYPEC_1_5A;
				pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_1] =
					CCG_PD_CMD_SET_TYPEC_1_5A;

				port_pdo_is_changed |= BIT(pd_port);
			}

			/**
			 * If there is no 3A port, the disconnected 1.5A port needs reset PDO to 3A
			 */
			if (!other_port_is_3A &&
			pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] ==
			    CCG_PD_CMD_SET_TYPEC_1_5A) {
				cypd_select_pdo(PORT_TO_CONTROLLER(pd_port),
					PORT_TO_CONTROLLER_PORT(pd_port), CCG_PD_CMD_SET_TYPEC_3A);
				pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] =
					CCG_PD_CMD_SET_TYPEC_3A;
				pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_1] =
					CCG_PD_CMD_SET_TYPEC_3A;
				port_pdo_is_changed &= ~BIT(pd_port);
			}

			/**
			 * When disconnected port is 3A port, need reset other disconnect port to 3A
			 */
			if (pd_port_states[pd_port].max_operating_current > 1500 &&
			    pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] ==
			    CCG_PD_CMD_SET_TYPEC_3A) {
				for (int p = 0; p < PD_PORT_COUNT; p++) {
					if ((p != pd_port) && (pd_port_states[p].c_state ==
					    CCG_STATUS_NOTHING)) {
						cypd_select_pdo(PORT_TO_CONTROLLER(p),
						    PORT_TO_CONTROLLER_PORT(p),
						    CCG_PD_CMD_SET_TYPEC_3A);
						pd_port_states[p].safety_table
						[TYPEC_SAFETY_LEVEL_0] = CCG_PD_CMD_SET_TYPEC_3A;
						pd_port_states[p].safety_table
						[TYPEC_SAFETY_LEVEL_1] = CCG_PD_CMD_SET_TYPEC_3A;
						port_pdo_is_changed &= ~BIT(p);
					}
				}
			}
		}
	}
}
