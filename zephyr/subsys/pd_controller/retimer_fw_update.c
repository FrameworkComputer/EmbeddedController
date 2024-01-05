/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Source file for retimer firmware update using Power Delivery chip.
 */

#include "builtin/assert.h"
#include "usb_mux.h"
#include "usbc/utils.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <atomic.h>
#include <drivers/pdc.h>
#include <usbc/pd_task_intel_altmode.h>
#include <usbc/retimer_fw_update.h>

/*
 * Update retimer firmware of no device attached(NDA) ports
 * On the EC side:
 * Retimer firmware update initiated by AP. ACPI_WRITE used
 * for requesting to process operation.
 * Order of operations requested by AP:
 * 1. USB_RETIMER_FW_UPDATE_GET_MUX
 * 2. USB_RETIMER_FW_UPDATE_SUSPEND_PD
 * 3. USB_RETIMER_FW_UPDATE_SET_USB
 * 4. USB_RETIMER_FW_UPDATE_SET_SAFE
 * 5. USB_RETIMER_FW_UPDATE_SET_TBT
 * 6. USB_RETIMER_FW_UPDATE_DISCONNECT
 * 7. USB_RETIMER_FW_UPDATE_RESUME_PD
 *
 * After every request to process operation, AP polls for the
 * result of the last operation. If desired result is not found
 * after several attempts, the procedure is aborted by AP and no
 * further operations will be requested.
 * Operation 1-4 and 6 are processed immediately. Operation 5
 * and 7 are deferred using work queue.
 * After operation 2, operation 3 and 4 modify virtual USB mux
 * and the altmode changes are queued until operation 5 sends
 * I2C command to PD which brings altmode changes into effect.
 *
 * After step 2 till step 5, typec events are blocked since
 * pd_altmode_task is suspended. After step 5, pd_altmode_task
 * is resumed to catch up with altmode changes brought by PD due to
 * entering retimer firmware update mode. After entering retimer
 * firmware update mode the PD spec requires it to block all typec
 * events.
 *
 * On the host side:
 * 1. Put NDA port into offline mode.
 *    This forces retimer to power on, and requests EC to suspend
 *    PD port, set USB mux to USB, Safe then TBT.
 * 2. Scan for retimers
 * 3. Update retimer NVM firmware.
 * 4. Authenticate.
 * 5. Wait 5 or more seconds for retimer to come back.
 * 6. Put NDA ports into online mode -- the functional state.
 *    This requestes EC to disconnect(set USB mux to 0), resume PD port.
 *
 * The order of requests from host are:
 *
 * Port 0 offline
 * Port 0 rescan retimers
 * Port 1 offline
 * Port 1 rescan retimers
 * ...
 * Port 0 online
 * Port 1 online
 * ...
 */

#define PD_CHIP_ENTRY(usbc_id, pd_id) \
	[USBC_PORT_NEW(usbc_id)] = DEVICE_DT_GET(pd_id),

#define PD_CHIP_NULL(usbc_id) [USBC_PORT_NEW(usbc_id)] = NULL,

#define CHECK_PD_CHIP(usbc_id)                                          \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, pdc),                     \
		    (PD_CHIP_ENTRY(usbc_id, DT_PHANDLE(usbc_id, pdc))), \
		    (PD_CHIP_NULL))

#define CHECK_RETIMER(usbc_id)                                              \
	COND_CODE_1(DT_PROP(usbc_id, pd_retimer), (CHECK_PD_CHIP(usbc_id)), \
		    (PD_CHIP_NULL))

#define PD_RETIMER(usbc_id)                                \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, pd_retimer), \
		    (CHECK_RETIMER(usbc_id)), ())

/* Flags to check retimer firmware update status */
#define USB_PD_RETIMER_FW_UPDATE_ERROR 0
#define USB_PD_RETIMER_FW_UPDATE_RUN 1
#define USB_PD_RETIMER_FW_UPDATE_LTD_RUN 2

LOG_MODULE_REGISTER(RETIMER_FWUPD, LOG_LEVEL_ERR);

/* Retimer state before, while or after firmware update*/
enum retimer_states {
	RETIMER_ABSENT = -1,
	RETIMER_ONLINE,
	RETIMER_OFFLINE,
	RETIMER_ONLINE_REQUESTED
};

/* Work queue info for specific port */
struct retimer_update_workq_info {
	struct k_work retimer_update_workq;
	int port;
};

/* Get Power Delivery chip device object retimer is connected to */
static const struct device *pd_retimer_ports[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, PD_RETIMER) };
BUILD_ASSERT(ARRAY_SIZE(pd_retimer_ports) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* Last operation received from AP via ACPI_WRITE for the last port */
static int last_op;
/* Result of last operation of last port requested by AP*/
static int last_result;
/* Last port AP requested operation for */
static int last_port;
/* Retimer firmware update status to track progress of deferred functions */
static atomic_t fw_update_status;
/* State of retimer of respective port */
static int retimer_state[CONFIG_USB_PD_PORT_MAX_COUNT];
/* Bitmask for ports with retimer firmware updatable */
static int port_info;

/*
 * Since AP requests retimer offline one port at a time, separate instance
 * of retimer_update_workq_info not required for each port.
 */
/* retimer_update_workq_info instance for Entering retimer fw update */
static struct retimer_update_workq_info enter_workq_info;
/* retimer_update_workq_info instance for Exiting retimer fw update */
static struct retimer_update_workq_info exit_workq_info;

/* Initialize retimer states of ports */
static int pd_retimer_state_init(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (pd_retimer_ports[i] != NULL) {
			retimer_state[i] = RETIMER_ONLINE;
			port_info |= BIT(i);
		} else {
			retimer_state[i] = RETIMER_ABSENT;
		}
	}

	return 0;
}
SYS_INIT(pd_retimer_state_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Check from dts if retimer connected to pd */
static bool pd_retimer_present(int port)
{
	return retimer_state[port] != RETIMER_ABSENT;
}

/* TODO(b:317295498): To be handled in PD_TASK */
static void enter_retimer_fw_update(struct k_work *work_item)
{
	struct retimer_update_workq_info *workq_info =
		CONTAINER_OF(work_item, struct retimer_update_workq_info,
			     retimer_update_workq);

	/*
	 * Write to PD chip via I2C. PD goes to retimer firmware
	 * update mode and sends I2C command to the retimer to
	 * go to firmware update mode.
	 */
	int val =
		pdc_update_retimer_fw(pd_retimer_ports[workq_info->port], true);
	if (val != 0)
		LOG_ERR("Enter Retimer firmware update mode failed");

	resume_pd_intel_altmode_task();
}

static void exit_retimer_fw_update(struct k_work *work_item)
{
	struct retimer_update_workq_info *workq_info =
		CONTAINER_OF(work_item, struct retimer_update_workq_info,
			     retimer_update_workq);

	/* PD exits retimer firmware update mode */
	int val = pdc_update_retimer_fw(pd_retimer_ports[workq_info->port],
					false);
	if (val != 0)
		LOG_ERR("Enter Retimer firmware update mode failed");

	/* Clear fw_update_status */
	atomic_clear_bit(&fw_update_status, USB_PD_RETIMER_FW_UPDATE_LTD_RUN);
	atomic_clear_bit(&fw_update_status, USB_PD_RETIMER_FW_UPDATE_RUN);

	/* Resume PD altmode task */
	resume_pd_intel_altmode_task();
}

int usb_retimer_fw_update_get_result(void)
{
	if (last_port < 0 && last_port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return USB_RETIMER_FW_UPDATE_ERR;

	/* Check if any retimer present */
	if (!pd_retimer_present(last_port))
		return USB_RETIMER_FW_UPDATE_ERR;

	/*
	 * Check retimer firmware update status flag.
	 * TODO(b:317507791) - Error Recovery for update.
	 */
	if (atomic_test_bit(&fw_update_status, USB_PD_RETIMER_FW_UPDATE_ERROR))
		return USB_RETIMER_FW_UPDATE_ERR;

	switch (last_op) {
	case USB_RETIMER_FW_UPDATE_QUERY_PORT:
		last_result = port_info;
		break;
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		if (!atomic_test_bit(&fw_update_status,
				     USB_PD_RETIMER_FW_UPDATE_LTD_RUN))
			last_result = 1;
		else
			last_result = USB_RETIMER_FW_UPDATE_INVALID_MUX;
		break;
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
		last_result = is_pd_intel_altmode_task_suspended() ?
				      0 :
				      USB_RETIMER_FW_UPDATE_INVALID_MUX;
		break;
	case USB_RETIMER_FW_UPDATE_SET_USB:
		last_result =
			(usb_mux_get(last_port) & USB_PD_MUX_USB_ENABLED) ?
				(usb_mux_get(last_port) &
				 USB_PD_MUX_USB_ENABLED) :
				USB_RETIMER_FW_UPDATE_INVALID_MUX;
		break;
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
		last_result = (usb_mux_get(last_port) & USB_PD_MUX_SAFE_MODE) ?
				      (usb_mux_get(last_port) &
				       USB_PD_MUX_SAFE_MODE) :
				      USB_RETIMER_FW_UPDATE_INVALID_MUX;
		break;
	case USB_RETIMER_FW_UPDATE_SET_TBT:
		last_result = (usb_mux_get(last_port) &
			       USB_PD_MUX_TBT_COMPAT_ENABLED) ?
				      (usb_mux_get(last_port) &
				       USB_PD_MUX_TBT_COMPAT_ENABLED) :
				      USB_RETIMER_FW_UPDATE_INVALID_MUX;
		break;
	case USB_RETIMER_FW_UPDATE_GET_MUX:
		last_result = (usb_mux_get(last_port) &
			       USB_RETIMER_FW_UPDATE_MUX_MASK) ==
					      USB_PD_MUX_NONE ?
				      USB_PD_MUX_NONE :
				      -1;
		break;
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		last_result = (usb_mux_get(last_port) &
			       USB_RETIMER_FW_UPDATE_MUX_MASK) ==
					      USB_PD_MUX_NONE ?
				      USB_PD_MUX_NONE :
				      USB_RETIMER_FW_UPDATE_INVALID_MUX;
		break;
	default:
		break;
	}

	return last_result;
}

void usb_retimer_fw_update_process_op(int port, int op)
{
	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	last_op = op;
	last_port = port;

	if (!pd_retimer_present(port))
		return;

	/* Clear retimer firmware update status error bit */
	atomic_clear_bit(&fw_update_status, USB_PD_RETIMER_FW_UPDATE_ERROR);

	switch (op) {
	case USB_RETIMER_FW_UPDATE_QUERY_PORT:
		break;
	case USB_RETIMER_FW_UPDATE_GET_MUX:
		if (retimer_state[port] != RETIMER_ONLINE)
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		break;
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
		if (retimer_state[port] == RETIMER_ONLINE) {
			retimer_state[port] = RETIMER_OFFLINE;
			/* Suspend PD altmode task to ignore altmode events */
			suspend_pd_intel_altmode_task();
		} else {
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		}
		break;
	case USB_RETIMER_FW_UPDATE_SET_USB:
		if (retimer_state[port] == RETIMER_OFFLINE)
			usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
				    USB_SWITCH_CONNECT, pd_get_polarity(port));
		else
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		break;
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
		if (retimer_state[port] == RETIMER_OFFLINE)
			usb_mux_set(port, USB_PD_MUX_SAFE_MODE,
				    USB_SWITCH_CONNECT, pd_get_polarity(port));
		else
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		break;
	case USB_RETIMER_FW_UPDATE_SET_TBT:
		if (retimer_state[port] == RETIMER_OFFLINE) {
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_RUN);
			enter_workq_info.port = port;
			k_work_init(&enter_workq_info.retimer_update_workq,
				    enter_retimer_fw_update);
			k_work_submit(&enter_workq_info.retimer_update_workq);
		} else {
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		}
		break;
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		if (retimer_state[port] == RETIMER_OFFLINE) {
			retimer_state[port] = RETIMER_ONLINE_REQUESTED;
			/* Suspend PD altmode task to ignore altmode events */
			suspend_pd_intel_altmode_task();
			usb_mux_set(port, USB_PD_MUX_NONE,
				    USB_SWITCH_DISCONNECT,
				    pd_get_polarity(port));
		} else {
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		}
		break;
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		if (retimer_state[port] == RETIMER_ONLINE_REQUESTED) {
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_LTD_RUN);
			exit_workq_info.port = port;
			k_work_init(&exit_workq_info.retimer_update_workq,
				    exit_retimer_fw_update);
			k_work_submit(&exit_workq_info.retimer_update_workq);
			retimer_state[port] = RETIMER_ONLINE;
		} else {
			atomic_set_bit(&fw_update_status,
				       USB_PD_RETIMER_FW_UPDATE_ERROR);
		}
		break;
	default:
		break;
	}
}
