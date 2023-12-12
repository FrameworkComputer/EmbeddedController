/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci_intf.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <bsp_helper.h>
#include <host_bsp_service.h>
#include <sedi_driver_ipc.h>
#include <sedi_driver_rtc.h>

LOG_MODULE_REGISTER(sys_mng, CONFIG_HECI_LOG_LEVEL);

/* indication for the field sequence of host utc and system time in message */
#define TFMT_SYSTEM_TIME 1

#define MNG_RX_CMPL_ENABLE 0
#define MNG_RX_CMPL_DISABLE 1
#define MNG_RX_CMPL_INDICATION 2
#define MNG_RESET_NOTIFY 3
#define MNG_RESET_NOTIFY_ACK 4
#define MNG_TIME_UPDATE 5
#define MNG_RESET_REQUEST 6
#define MNG_RTD3_NOTIFY 7
#define MNG_RTD3_NOTIFY_ACK 8
#define MNG_D0_NOTIFY 9
#define MNG_D0_NOTIFY_ACK 10
#define MNG_CORE_INFO_REQ 11
#define MNG_CORE_INFO_RESP 12
#define MNG_ILLEGAL_CMD 0xFF
#define MAX_MNG_MSG_LEN 128

#define MNG_CAP_RESET_REQ_SUPPORTED BIT(0)
#define MNG_CAP_LOAD_FW_SUPPORTED BIT(1)
#define MNG_CAP_ROUTE_IPC_SUPPORTED BIT(2)
#define MNG_CAP_RTD3_SUPPORTED BIT(3)

#define HOST_COMM_REG 0x40400038
#define HOST_RDY_BIT 7
#define IS_HOST_UP(host_comm_reg) (host_comm_reg & BIT(HOST_RDY_BIT))

#define TIME_FORMAT_UTC 0
#define TIME_FORMAT_SYSTEM_TIME 1

static bool rx_complete_enabled = true;
static bool rx_complete_changed;

struct reset_payload_tpye {
	uint16_t reset_id;
	uint16_t capabilities;
};

struct core_info {
	uint16_t core_id;
	uint16_t router_bitmap;
	uint16_t max_frag_size;
	uint16_t reserved;
};

#if CONFIG_RTD3
#include <sedi_driver_pm.h>
#define MIN_RESET_INTV 100000

#define DSTATE_0 0
#define DSTATE_RTD3_NOTIFIED 1
#define DSTATE_RTD3 2

K_SEM_DEFINE(sem_d3, 0, 1);
K_SEM_DEFINE(sem_rtd3, 1, 1);
atomic_t is_waiting_d3 = ATOMIC_INIT(0);

static int mng_host_req_d0(uint32_t timeout);
/*!
 * \fn int mng_host_access_req()
 * \brief request access to host
 * \param[in] timeout: timeout time
 * \return 0 or error codes
 */
int (*mng_host_access_req)(uint32_t /* timeout */) = mng_host_req_d0;

static int mng_host_req_d0(uint32_t timeout)
{
	return k_sem_take(&sem_rtd3, K_MSEC(timeout));
}

static int mng_host_req_rtd3(uint32_t timeout)
{
	sedi_pm_trigger_pme(0);
	LOG_DBG("PME wake is triggered\n");
	return k_sem_take(&sem_rtd3, K_MSEC(timeout));
}

static int mng_host_req_rtd3_notified(uint32_t timeout)
{
	LOG_DBG("RTD3 notified state\n");
	atomic_inc(&is_waiting_d3);
	k_sem_take(&sem_d3, K_MSEC(timeout));
	if (mng_host_access_req == mng_host_req_rtd3_notified) {
		atomic_set(&is_waiting_d3, 0);
		LOG_DBG("Failed to get out RTD3_notified state in %d ms!\n",
			timeout);
		return RTD3_NOTIFIED_STUCK;
	} else {
		return mng_host_access_req(timeout);
	}
}

static void mng_d3_proc(sedi_pm_d3_event_t d3_event, void *ctx)
{
	ARG_UNUSED(ctx);

	switch (d3_event) {
	case PM_EVENT_HOST_RTD3_ENTRY:
		mng_host_access_req = mng_host_req_rtd3;
		if (atomic_set(&is_waiting_d3, 0)) {
			k_sem_give(&sem_d3);
		}
		LOG_DBG("RTD3_ENTRY received!\n");
		break;

	case PM_EVENT_HOST_RTD3_EXIT:
		mng_host_access_req = mng_host_req_d0;
		LOG_DBG("RTD3_EXIT received!\n");
		break;
	default:
		break;
	}
}

void mng_host_access_dereq(void)
{
	k_sem_give(&sem_rtd3);
}

#endif /* endif of CONFIG_RTD3*/

#if CONFIG_HOST_TIME_SYNC

struct host_clock_data {
	uint64_t primary_host_time;
	struct {
		uint8_t primary_source;
		uint8_t secondary_source;
		uint16_t reserved;
	} time_format;
	uint64_t secondary_host_time;
};

struct saved_time_t {
	uint64_t last_sync_host_clock_utc;
	uint64_t last_sync_host_clock_sys;
	uint64_t last_sync_fw_clock;
};
struct saved_time_t saved_time;

static void handle_host_time_sync(uint8_t *data, uint8_t data_len)
{
	struct host_clock_data *const sync_data =
		(struct host_clock_data *const)data;

	/* process system time sync  */
	if (data_len == sizeof(struct host_clock_data)) {
		/* new sync format, used when host is windows */
		saved_time.last_sync_fw_clock = sedi_rtc_get_us();
		if (sync_data->time_format.primary_source == TFMT_SYSTEM_TIME) {
			saved_time.last_sync_host_clock_sys =
				sync_data->primary_host_time;
			saved_time.last_sync_host_clock_utc =
				sync_data->secondary_host_time;
		} else {
			saved_time.last_sync_host_clock_sys =
				sync_data->secondary_host_time;
			saved_time.last_sync_host_clock_utc =
				sync_data->primary_host_time;
		}
	} else if (data_len == sizeof(sync_data->primary_host_time)) {
		/* old sync format, used when host is linux */
		saved_time.last_sync_fw_clock = sedi_rtc_get_us();
		saved_time.last_sync_host_clock_sys =
			sync_data->primary_host_time;
		saved_time.last_sync_host_clock_utc =
			sync_data->primary_host_time;
	} else {
		LOG_ERR("Unknown time sync format");
	}
}

void get_clock_sync_data(uint64_t *last_fw_clock, uint64_t *last_host_clock_utc,
			 uint64_t *last_host_clock_system)
{
	if (last_fw_clock) {
		*last_fw_clock = saved_time.last_sync_fw_clock;
	}
	if (last_host_clock_utc) {
		*last_host_clock_utc = saved_time.last_sync_host_clock_utc;
	}
	if (last_host_clock_system) {
		*last_host_clock_system = saved_time.last_sync_host_clock_sys;
	}
}

#endif

static int send_reset_to_peer(uint32_t command, uint16_t reset_id)
{
	int ret;
	uint16_t capability = MNG_CAP_RESET_REQ_SUPPORTED;

#ifdef CONFIG_HECI_ROUTER
	capability |= MNG_CAP_ROUTE_IPC_SUPPORTED;
#endif

#ifdef CONFIG_RTD3
	capability |= MNG_CAP_RTD3_SUPPORTED;
#endif

	struct reset_payload_tpye mng_msg = { .reset_id = reset_id,
					      .capabilities = capability };

	uint32_t drbl = BUILD_MNG_DRBL(command, sizeof(mng_msg));

	LOG_DBG("");
	LOG_HEXDUMP_DBG((uint8_t *)&mng_msg, sizeof(mng_msg), "outcoming");
	ret = host_intf->send_msg(drbl, (uint8_t *)&mng_msg, sizeof(mng_msg));
	return ret;
}

int send_rx_complete(void)
{
	int ret = 0;
	uint32_t rx_comp_drbl = BUILD_MNG_DRBL(MNG_RX_CMPL_INDICATION, 0);

	if (rx_complete_enabled) {
		ret = host_intf->send_msg(rx_comp_drbl, NULL, 0);
		if (ret) {
			LOG_ERR("fail to send rx_complete msg");
		}
	}

	return ret;
}

static uint8_t mng_in_msg[MAX_MNG_MSG_LEN];
static int sys_mng_handler(uint32_t drbl)
{
	int cmd = HEADER_GET_MNG_CMD(drbl);
	struct reset_payload_tpye *rst_msg;
	struct core_info info = { 0 };

	LOG_DBG("received a management msg, drbl = %08x", drbl);
	__ASSERT(IPC_HEADER_GET_LENGTH(drbl) <= MAX_MNG_MSG_LEN, "bad mng msg");
	host_intf->read_msg(&drbl, mng_in_msg, IPC_HEADER_GET_LENGTH(drbl));
	host_intf->send_ack();
	send_rx_complete();

	LOG_HEXDUMP_DBG(mng_in_msg, IPC_HEADER_GET_LENGTH(drbl),
			"mng incoming");
	switch (cmd) {
	case MNG_RX_CMPL_ENABLE:
		rx_complete_enabled = true;
		rx_complete_changed = true;
		break;
	case MNG_RX_CMPL_DISABLE:
		rx_complete_enabled = false;
		rx_complete_changed = true;
		break;
	case MNG_RX_CMPL_INDICATION:
		/* not used yet */
		break;
#if CONFIG_RTD3
	case MNG_D0_NOTIFY:
		LOG_DBG("D0 warning received!\n");
		mng_host_access_req = mng_host_req_d0;
		/* 2 for D0 */
		sedi_pm_set_hostipc_event(SEDI_PM_HOSTIPC_D0_NOTIFY);
		host_intf->send_msg(BUILD_MNG_DRBL(MNG_D0_NOTIFY_ACK, 0), NULL,
				    0);
		break;
	case MNG_RTD3_NOTIFY:
		LOG_DBG("RTD3 warning received!\n");
		int rtd3_ready = !(k_sem_take(&sem_rtd3, K_NO_WAIT));

		if (rtd3_ready) {
			sedi_pm_set_hostipc_event(SEDI_PM_HOSTIPC_RTD3_NOTIFY);
			mng_host_access_req = mng_host_req_rtd3_notified;
		}
		host_intf->send_msg(BUILD_MNG_DRBL(MNG_RTD3_NOTIFY_ACK,
						   sizeof(uint32_t)),
				    (uint8_t *)&rtd3_ready, sizeof(uint32_t));
		break;
#endif
	case MNG_RESET_NOTIFY:
#if CONFIG_HECI
		heci_reset();
#endif
		rst_msg = (struct reset_payload_tpye *)mng_in_msg;
		send_reset_to_peer(MNG_RESET_NOTIFY_ACK, rst_msg->reset_id);
		__fallthrough;
	case MNG_RESET_NOTIFY_ACK:
		if (host_intf->set_ready)
			host_intf->set_ready(1);
		LOG_DBG("link is up");
		break;
	case MNG_TIME_UPDATE:
#if CONFIG_HOST_TIME_SYNC
		handle_host_time_sync(mng_in_msg, IPC_HEADER_GET_LENGTH(drbl));
#endif
		break;
	case MNG_RESET_REQUEST:
		sys_reboot(SYS_REBOOT_COLD);
		LOG_DBG("host requests to reset, not support, do nothing");
		break;

	case MNG_CORE_INFO_REQ:
		info.core_id = CONFIG_HECI_CORE_ID;
		info.router_bitmap = (uint16_t)get_heci_core_bitmap() &
				     (~BIT(CONFIG_HECI_CORE_ID));
		info.max_frag_size =
			host_intf->max_fragment_size + sizeof(uint32_t);
		host_intf->send_msg(BUILD_MNG_DRBL(MNG_CORE_INFO_RESP,
						   sizeof(info)),
				    (uint8_t *)&info, sizeof(info));
		break;
	default:
		LOG_ERR("invaild sysmng cmd, cmd = %02x", cmd);
		return -1;
	}
	return 0;
}

static int sys_boot_handler(uint32_t drbl)
{
	host_intf->send_ack();
	send_rx_complete();
	if (drbl == BIT(DRBL_BUSY_OFFS)) {
		send_reset_to_peer(MNG_RESET_NOTIFY, 0);
	}

	return 0;
}

int mng_and_boot_init(void)
{
	int ret;

#if CONFIG_RTD3
	sedi_pm_register_d3_notification(0, mng_d3_proc, NULL);
#endif
	ret = host_protocol_register(PROTOCOL_MNG, sys_mng_handler);
	if (ret != 0) {
		LOG_ERR("fail to add sys_mng_handler as cb fun");
		return -1;
	}
	ret = host_protocol_register(PROTOCOL_BOOT, sys_boot_handler);
	if (ret != 0) {
		LOG_ERR("fail to add sys_boot_handler as cb fun");
		return -1;
	}
	LOG_DBG("register system message handler successfully");

	send_reset_to_peer(MNG_RESET_NOTIFY, 0);
	return 0;
}
