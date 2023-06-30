/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci_router.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(host, CONFIG_HECI_LOG_LEVEL);

#define HECI_RTABLE_ENTRIES 8
heci_bsp_t *heci_rtable[HECI_RTABLE_ENTRIES];

uint8_t router_buffer[4096];

void heci_router_init(void)
{
	heci_bsp_t *entry = NULL;

	for (int core_id = 0; core_id < HECI_RTABLE_ENTRIES; core_id++) {
		entry = heci_intf_get_entry(core_id);
		heci_rtable[core_id] = entry;
		if (entry && entry->init) {
			entry->init();
		}
	}
}

void dispatch_msg_to_core(heci_bsp_t *bsp_intf)
{
	uint32_t inbound_drbl = 0, core_id, length;
	int ret;

	ret = bsp_intf->read_msg(&inbound_drbl, NULL, 0);
	if (ret != 0) {
		return;
	}
	core_id = HEADER_GET_COREID(inbound_drbl);

	if (core_id >= HECI_RTABLE_ENTRIES) {
		LOG_ERR("not valid msg core id");
		return;
	}

	length = HEADER_GET_LENGTH(inbound_drbl);
	if (bsp_intf == host_intf) {
		/* downstream messages */
		if (core_id == CONFIG_HECI_CORE_ID) {
			process_host_msgs();
		} else {
			LOG_DBG("host->%d drbl = %08x", core_id, inbound_drbl);
			bsp_intf->read_msg(NULL, router_buffer, length);
			LOG_HEXDUMP_DBG(router_buffer, length, "downstreaming");
			heci_rtable[core_id]->send_msg(inbound_drbl,
						       router_buffer, length);
			bsp_intf->send_ack();
		}
	} else {
		if (heci_rtable[core_id] != bsp_intf) {
			LOG_ERR("not valid msg upstreaming interface");
			return;
		}
		LOG_DBG("%d->host drbl = %08x", core_id, inbound_drbl);
		/* upstream messages */
		bsp_intf->read_msg(NULL, router_buffer, length);
		LOG_HEXDUMP_DBG(router_buffer, length, "upstreaming");
		host_intf->send_msg(inbound_drbl, router_buffer, length);
		bsp_intf->send_ack();
	}
}
