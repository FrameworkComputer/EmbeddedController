/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bsp_helper.h"
#include "heci_intf.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(heci, CONFIG_HECI_LOG_LEVEL);
#define HECI_RTABLE_ENTRIES 8
K_MSGQ_DEFINE(heci_msg_queue, sizeof(struct heci_bsp_t *), HECI_RTABLE_ENTRIES,
	      sizeof(struct heci_bsp_t *));

struct heci_bsp_t *heci_intf_get_entry(int core_id)
{
	for (struct heci_bsp_t *bsp = &__heci_desc_start;
	     bsp < &__heci_desc_end; bsp++) {
		if (core_id == bsp->core_id) {
			return bsp;
		}
	}
	return NULL;
}

struct heci_bsp_t *get_host_intf(void)
{
	for (struct heci_bsp_t *bsp = &__heci_desc_start;
	     bsp < &__heci_desc_end; bsp++) {
		if (bsp->peer_is_host) {
			return bsp;
		}
	}
	return NULL;
}

uint32_t get_heci_core_bitmap(void)
{
	uint32_t map = 0;

	for (struct heci_bsp_t *bsp = &__heci_desc_start;
	     bsp < &__heci_desc_end; bsp++) {
		map |= BIT(bsp->core_id);
	}
	return map;
}

int host_svr_hal_init(void)
{
#ifdef CONFIG_HECI_ROUTER
	extern void heci_router_init(void);
	heci_router_init();
#else
	if (host_intf->init) {
		host_intf->init();
	}
#endif
	return 0;
}

int send_heci_newmsg_notify(struct heci_bsp_t *sender)
{
	int ret;
	const void *data = (const void *)&sender;

	ret = k_msgq_put(&heci_msg_queue, data, K_NO_WAIT);
	if (ret) {
		LOG_ERR("failed to handle incoming heci msg to q, ret = %d",
			ret);
	}
	return ret;
}

struct heci_bsp_t *wait_and_draw_heci_newmsg(void)
{
	void *sender = NULL;

	k_msgq_get(&heci_msg_queue, &sender, K_FOREVER);

	__ASSERT(sender, "invalid sender found");
	return sender;
}
