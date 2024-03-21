/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "hooks.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_tc_sm.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

static const uint32_t pd_src_pdo_1A5[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};

static const uint32_t pd_src_pdo_3A[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};

static inline int has_other_active_source(int port)
{
	int p, active_source = 0;

	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		if (p == port)
			continue;
		if (tc_is_attached_src(p))
			active_source++;
	}
	return active_source;
}

static inline int is_active_source(int port)
{
	return tc_is_attached_src(port);
}

static int can_supply_max_current(int port)
{
	return is_active_source(port) && !has_other_active_source(port);
}

int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	if (can_supply_max_current(port)) {
		*src_pdo = pd_src_pdo_3A;
		return ARRAY_SIZE(pd_src_pdo_3A);
	}

	*src_pdo = pd_src_pdo_1A5;

	return ARRAY_SIZE(pd_src_pdo_1A5);
}

int dpm_get_source_current(const int port)
{
	if (pd_get_power_role(port) == PD_ROLE_SINK)
		return 0;

	if (can_supply_max_current(port))
		return 3000;
	else if (typec_get_default_current_limit_rp(port) == TYPEC_RP_1A5)
		return 1500;
	else
		return 500;
}

static int port_status[CONFIG_USB_PD_PORT_MAX_COUNT];
static int port_pre_status[CONFIG_USB_PD_PORT_MAX_COUNT];

static void update_src_pdo_deferred(void);
DECLARE_DEFERRED(update_src_pdo_deferred);
static void update_src_pdo_deferred(void)
{
	int p, rp;

	/* Set port limit according to policy */
	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		rp = can_supply_max_current(p) ? TYPEC_RP_3A0 : TYPEC_RP_1A5;
		port_status[p] = rp;
	}

	/* 1. Check if there are 3A port need downgrade */
	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		if ((port_status[p] != port_pre_status[p]) &&
		    (port_pre_status[p] == TYPEC_RP_3A0)) {
			typec_set_source_current_limit(p, port_status[p]);
			typec_select_src_current_limit_rp(p, port_status[p]);
			typec_select_src_collision_rp(p, port_status[p]);
			pd_update_contract(p);
			port_pre_status[p] = port_status[p];
		}
	}

	/* 2. Excute other port status */
	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		if (port_status[p] != port_pre_status[p]) {
			typec_set_source_current_limit(p, port_status[p]);
			typec_select_src_current_limit_rp(p, port_status[p]);
			typec_select_src_collision_rp(p, port_status[p]);
			pd_update_contract(p);
			port_pre_status[p] = port_status[p];
		}
	}
}

static void manage_source_port(void)
{
	hook_call_deferred(&update_src_pdo_deferred_data, 0);
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, manage_source_port, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, manage_source_port, HOOK_PRIO_DEFAULT);

static void manage_source_port_power_change(void)
{
	/* for FRS device status change check */
	hook_call_deferred(&update_src_pdo_deferred_data, 500 * MSEC);
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, manage_source_port_power_change,
	     HOOK_PRIO_DEFAULT);
