/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/pdc.h>
#include <usbc/utils.h>

LOG_MODULE_REGISTER(pdc_dpm);

/*
 * Source-out policy variables and APIs
 *
 * Priority for the available 3.0 A ports is given in the following order:
 * - sink partners which report requiring > 1.5 A in their Sink_Capabilities
 * - source partners with FRS that request 3.0A as a sink
 * - non-pd sink partners
 */

/*
 * Bitmasks of port numbers in each following category
 *
 * Note: request bitmasks should be accessed atomically as other ports may alter
 * them
 */
static uint32_t max_current_claimed;

/* Ports with PD sink needing > 1.5 A */
static atomic_t sink_max_pdo_requested;
/* Ports with FRS source needing > 1.5 A */
static atomic_t source_frs_max_requested;
/* Ports with non-PD sinks, so current requirements are unknown */
static atomic_t non_pd_sink_max_requested;

static void pdc_dpm_balance_source_ports(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(dpm_work, pdc_dpm_balance_source_ports);

static K_MUTEX_DEFINE(max_current_claimed_mtx);

#define LOWEST_PORT(p) __builtin_ctz(p) /* Undefined behavior if p == 0 */

static int count_port_bits(uint32_t bitmask)
{
	int i, total = 0;

	for (i = 0; i < pdc_power_mgmt_get_usb_pd_port_count(); i++) {
		if (bitmask & BIT(i))
			total++;
	}

	return total;
}

/**
 * @brief Adjust source current allocations for usbc ports
 *
 * This function is called when new port partners are either added or removed
 * that could affect how source current limits per port are allocated. The
 * number of ports capable of sourcing 3.0A current will be defined by
 * CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS.
 *
 * Note that this function is called both from a PDC thread when new ports or
 * added/removed and from the system workqueue when the current
 * limit for a port is being reduced.
 */
static void pdc_dpm_balance_source_ports(struct k_work *work)
{
	uint32_t removed_ports;
	uint32_t new_ports;
	enum usb_typec_current_t rp;
	int rv;

	rv = k_work_busy_get(&dpm_work.work);
	/* check if work is delayed work is pending */
	if (rv && (rv & K_WORK_DELAYED)) {
		return;
	}

	k_mutex_lock(&max_current_claimed_mtx, K_FOREVER);

	/* Remove any ports which no longer require 3.0 A */
	removed_ports = max_current_claimed &
			~(sink_max_pdo_requested | source_frs_max_requested |
			  non_pd_sink_max_requested);
	max_current_claimed &= ~removed_ports;

	/* Allocate 3.0 A to new PD sink ports that need it */
	new_ports = sink_max_pdo_requested & ~max_current_claimed;

	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS) {
			max_current_claimed |= BIT(new_max_port);
			pdc_power_mgmt_set_current_limit(new_max_port,
							 TC_CURRENT_3_0A);
		} else if (non_pd_sink_max_requested & max_current_claimed) {
			/* Always downgrade non-PD ports first */
			int rem_non_pd = LOWEST_PORT(non_pd_sink_max_requested &
						     max_current_claimed);

			rp = pdc_power_mgmt_get_default_current_limit(
				rem_non_pd);
			pdc_power_mgmt_set_current_limit(rem_non_pd, rp);
			max_current_claimed &= ~BIT(rem_non_pd);

			/* Wait tSinkAdj before using current */
			k_work_reschedule(&dpm_work, K_MSEC(75));
			goto unlock;
		} else if (source_frs_max_requested & max_current_claimed) {
			/* Downgrade lowest FRS port from 3.0 A slot */
			int rem_frs = LOWEST_PORT(source_frs_max_requested &
						  max_current_claimed);

			pdc_power_mgmt_frs_enable(rem_frs, false);
			max_current_claimed &= ~BIT(rem_frs);

			/* Give 50 ms for the PD task to process DPM flag */
			k_work_reschedule(&dpm_work, K_MSEC(50));
			goto unlock;
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_max_port);
	}

	/* Allocate 3.0 A to any new FRS ports that need it */
	new_ports = source_frs_max_requested & ~max_current_claimed;
	while (new_ports) {
		int new_frs_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS) {
			max_current_claimed |= BIT(new_frs_port);
			/* Enable FRS for this port */
			pdc_power_mgmt_frs_enable(new_frs_port, true);
		} else if (non_pd_sink_max_requested & max_current_claimed) {
			int rem_non_pd = LOWEST_PORT(non_pd_sink_max_requested &
						     max_current_claimed);

			rp = pdc_power_mgmt_get_default_current_limit(
				rem_non_pd);
			pdc_power_mgmt_set_current_limit(rem_non_pd, rp);
			max_current_claimed &= ~BIT(rem_non_pd);

			/* Wait tSinkAdj before using current */
			k_work_reschedule(&dpm_work, K_MSEC(75));
			goto unlock;
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_frs_port);
	}

	/* Allocate 3.0 A to any non-PD ports which could need it */
	new_ports = non_pd_sink_max_requested & ~max_current_claimed;
	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS) {
			max_current_claimed |= BIT(new_max_port);
			pdc_power_mgmt_set_current_limit(new_max_port,
							 TC_CURRENT_3_0A);
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_max_port);
	}
unlock:
	k_mutex_unlock(&max_current_claimed_mtx);
}

/* Process port's first Sink_Capabilities PDO for port current consideration */
void pdc_dpm_eval_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo)
{
	/* Verify partner supplied valid vSafe5V fixed object first */
	if ((vsafe5v_pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

	if (PDO_FIXED_VOLTAGE(vsafe5v_pdo) != 5000)
		return;

	if (pdc_power_mgmt_get_power_role(port) == PD_ROLE_SOURCE) {
		if (CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 0)
			return;

		/* Valid PDO to process, so evaluate whether >1.5A is needed */
		if (PDO_FIXED_CURRENT(vsafe5v_pdo) <= 1500)
			return;

		atomic_set_bit(&sink_max_pdo_requested, port);
	} else {
		int frs_current = vsafe5v_pdo & PDO_FIXED_FRS_CURR_MASK;

		if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS))
			return;

		/* FRS is only supported in PD 3.0 and higher */
		if (pdc_power_mgmt_get_rev(port, TCPCI_MSG_SOP) == PD_REV20)
			return;

		if ((vsafe5v_pdo & PDO_FIXED_DUAL_ROLE) && frs_current) {
			/* Always enable FRS when 3.0 A is not needed */
			if (frs_current == PDO_FIXED_FRS_CURR_DFLT_USB_POWER ||
			    frs_current == PDO_FIXED_FRS_CURR_1A5_AT_5V) {
				pdc_power_mgmt_frs_enable(port, true);
				return;
			}

			if (CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 0)
				return;

			atomic_set_bit(&source_frs_max_requested, port);
		} else {
			return;
		}
	}

	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_add_non_pd_sink(int port)
{
	if (CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 0)
		return;

	atomic_set_bit(&non_pd_sink_max_requested, port);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_evaluate_request_rdo(int port, uint32_t rdo)
{
	int idx;
	int op_ma;

	if (CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 0)
		return;

	idx = RDO_POS(rdo);
	/* Check for invalid index */
	if (!idx)
		return;

	/* Extract the requested current which is report in mA/10 units */
	op_ma = 10 * ((rdo >> 10) & 0x3FF);
	if (atomic_test_bit(&sink_max_pdo_requested, port) && (op_ma <= 1500)) {
		/*
		 * sink_max_pdo_requested will be set when we get 5V/3A sink
		 * capability from port partner. If port partner only request
		 * 5V/1.5A, we need to provide 5V/1.5A.
		 */
		atomic_clear_bit(&sink_max_pdo_requested, port);
		pdc_dpm_balance_source_ports(&dpm_work.work);
	}
}

void pdc_dpm_remove_sink(int port)
{
	enum usb_typec_current_t rp;

	if (CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 0)
		return;

	if (!atomic_test_bit(&sink_max_pdo_requested, port) &&
	    !atomic_test_bit(&non_pd_sink_max_requested, port))
		return;

	atomic_clear_bit(&sink_max_pdo_requested, port);
	atomic_clear_bit(&non_pd_sink_max_requested, port);

	/* Restore selected default Rp on the port */
	rp = pdc_power_mgmt_get_default_current_limit(port);
	pdc_power_mgmt_set_current_limit(port, rp);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_remove_source(int port)
{
	if (CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 0)
		return;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS))
		return;

	if (!(BIT(port) & (uint32_t)source_frs_max_requested))
		return;

	atomic_clear_bit(&source_frs_max_requested, port);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}
