/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci_intf.h"
#include "host_service_common.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <bsp_helper.h>
#include <host_bsp_service.h>

LOG_MODULE_REGISTER(host, CONFIG_HECI_LOG_LEVEL);

#define SERVICE_STACK_SIZE 1600
struct k_thread host_service_thread;

K_THREAD_STACK_DEFINE(host_service_stack, SERVICE_STACK_SIZE);

static void heci_rx_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct heci_bsp_t *incoming_intf = NULL;

#if (CONFIG_HOST_SERVICE_BOOT_DELAY)
	k_sleep(K_SECONDS(CONFIG_HOST_SERVICE_BOOT_DELAY));
#endif

	LOG_DBG("local-host service started");

	heci_init(NULL);
#ifdef CONFIG_SYS_MNG
	mng_and_boot_init(NULL);
#endif

	while (true) {
		incoming_intf = wait_and_draw_heci_newmsg();
		LOG_DBG(" comes new msg from core %p", incoming_intf);
#ifdef CONFIG_HECI_ROUTER
		dispatch_msg_to_core(incoming_intf);
#else
		if (incoming_intf == host_intf) {
			process_host_msgs();
		}
#endif
	}
}

static int host_config(void)
{
	LOG_DBG("");

	host_intf = heci_intf_get_entry(CONFIG_HECI_CORE_ID);
	LOG_DBG("host intf = %p", host_intf);
	if (host_intf == NULL) {
		LOG_ERR("no hw interfaces found to host");
		return -1;
	}

	host_svr_hal_init();
	k_thread_create(&host_service_thread, host_service_stack,
			K_THREAD_STACK_SIZEOF(host_service_stack), heci_rx_task,
			NULL, NULL, NULL, K_PRIO_COOP(1), 0, K_FOREVER);
	k_thread_name_set(&host_service_thread, "host_service");
	return 0;
}

static int host_service_init(void)
{
	int ret;

	ret = host_config();
	if (ret != 0) {
		return ret;
	}
	k_thread_start(&host_service_thread);
	return 0;
}

SYS_INIT(host_service_init, APPLICATION, 99);
