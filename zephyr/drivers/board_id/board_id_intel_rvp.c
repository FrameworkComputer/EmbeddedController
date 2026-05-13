/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/rvp_board_id.h>

#define DT_DRV_COMPAT intel_rvp_board_id

#define BOM_GPIOS_COUNT 3
#define FAB_GPIOS_COUNT 2
#define BOARD_GPIOS_COUNT 6

#define FAB_ID_SHIFT 8
#define BOARD_ID_MASK (BIT(BOARD_GPIOS_COUNT) - 1)

LOG_MODULE_DECLARE(rvp_model_id, LOG_LEVEL_DBG);

static int rvp_model_id = -1;

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "Unsupported RVP Board ID instance");

#if DT_NODE_HAS_PROP(DT_DRV_INST(0), bom_gpios)
BUILD_ASSERT(DT_INST_PROP_LEN(0, bom_gpios) == BOM_GPIOS_COUNT,
	     "Incorrect bom gpios count.");
#endif

#if DT_NODE_HAS_PROP(DT_DRV_INST(0), fab_gpios)
BUILD_ASSERT(DT_INST_PROP_LEN(0, fab_gpios) == FAB_GPIOS_COUNT,
	     "Incorrect fab gpios count.");
#endif

BUILD_ASSERT(DT_INST_PROP_LEN(0, board_gpios) == BOARD_GPIOS_COUNT,
	     "Incorrect board gpios count.");

#define FOREACH_RVP_GPIOS_ELEM(inst, prop) \
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, prop, GPIO_DT_SPEC_GET_BY_IDX, (, ))

const struct rvp_board_id_config *rvp_config;

struct rvp_board_id_config {
	const struct gpio_dt_spec *bom_gpios_config;
	const struct gpio_dt_spec *fab_gpios_config;
	const struct gpio_dt_spec *board_gpios_config;
	rvp_board_id_handler handler;
};

/*
 * Returns board_id, bom_id or fab_id information based on the request on
 * success -1 on error.
 */
int get_rvp_id_config(enum rvp_id_type id_type)
{
	const struct device *gpio_port;

	gpio_port = rvp_config->board_gpios_config[0].port;
	if (!device_is_ready(gpio_port)) {
		LOG_ERR_DEVICE_NOT_READY(gpio_port);
		return -ENODEV;
	}

	if (id_type == BOARD_ID) {
		/*
		 * BOARD ID[5:0] : IOEX[13:8]
		 */
		int board_id = 0;
		for (int i = 0; i < BOARD_GPIOS_COUNT; ++i) {
			int pin = gpio_pin_get_dt(
				&rvp_config->board_gpios_config[i]);
			if (pin < 0)
				return pin;
			board_id |= pin << i;
		}

		LOG_DBG("RVP_ID: BOARD_ID: 0x%x", board_id);
		return board_id;
	}

	if (id_type == BOM_ID && rvp_config->bom_gpios_config) {
		/*
		 * BOM ID [2]   : IOEX[0]
		 * BOM ID [1:0] : IOEX[15:14]
		 */
		int bom_id = 0;
		for (int i = 0; i < BOM_GPIOS_COUNT; ++i) {
			int pin = gpio_pin_get_dt(
				&rvp_config->bom_gpios_config[i]);
			if (pin < 0)
				return pin;
			bom_id |= pin << i;
		}

		LOG_DBG("RVP_ID: BOM_ID: 0x%x", bom_id);
		return bom_id;
	}

	if (id_type == FAB_ID && rvp_config->fab_gpios_config) {
		/*
		 * FAB ID [1:0] : IOEX[2:1] + 1
		 */
		int fab_id = 0;
		for (int i = 0; i < FAB_GPIOS_COUNT; ++i) {
			int pin = gpio_pin_get_dt(
				&rvp_config->fab_gpios_config[i]);
			if (pin < 0)
				return pin;
			fab_id |= pin << i;
		}
		fab_id += 1;

		LOG_DBG("RVP_ID: FAB_ID: 0x%x", fab_id);
		return fab_id;
	}

	return -1;
}

void rvp_id_handler(void)
{
	int board_id;
	int fab_id;
	int ret;

	board_id = get_rvp_id_config(BOARD_ID);
	if (board_id < 0) {
		LOG_ERR("RVP_ID: get_rvp_id_config.BOARD_ID failed");
		return;
	}

	fab_id = get_rvp_id_config(FAB_ID);
	if (fab_id < 0) {
		LOG_ERR("RVP_ID: get_rvp_id_config.FAB_ID failed");
		return;
	}

	rvp_model_id = (fab_id << FAB_ID_SHIFT) | board_id;

	LOG_DBG("RVP_ID: from GPIOs: %d", rvp_model_id);

	uint32_t id_from_cbi;
	if ((cbi_get_model_id(&id_from_cbi) == EC_SUCCESS) &&
	    id_from_cbi == rvp_model_id) {
		/* CBI MODEL_ID is up-to-date */
		LOG_DBG("RVP_ID: %d matches CBI", rvp_model_id);
		return;
	}

	LOG_INF("RVP_ID: store in CBI: %d", rvp_model_id);

	ret = cbi_set_model_id(rvp_model_id);
	if (ret) {
		LOG_ERR("RVP_ID: cbi_set_model_id() failed: %d", ret);
	}
}

/*
 * Returns board version on success, -1 on error.
 */
__override int board_get_version(void)
{
	if (rvp_model_id == -1) {
		int id;

		if (cbi_get_model_id(&id) != EC_SUCCESS) {
			LOG_INF("RVP_ID: not available");
			return -1;
		}

		LOG_DBG("RVP_ID: %d load from CBI", id);
		rvp_model_id = id;
	}

	/* return only board id from model id */
	return rvp_model_id & BOARD_ID_MASK;
}

#define SAFE_DEV_NAME(dev) ((dev) && ((dev)->name) ? (dev)->name : "unknown")

#ifdef CONFIG_AP_PWRSEQ_DRIVER
static void ap_power_state_callback(const struct device *dev,
				    const enum ap_pwrseq_state entry,
				    const enum ap_pwrseq_state exit)
{
	const struct device *gpio_port;
	int ret;

	if (!DT_INST_NODE_HAS_PROP(0, defer_until_s5)) {
		return;
	}

	if (entry > AP_POWER_STATE_S5) {
		LOG_DBG("RVP_ID: S5 callback triggered. Init GPIO drivers.");

		/* Sleep a short amount of time to allow the external I/O
		 * expander to power up and be ready to accept commands. */
		k_sleep(K_MSEC(25));

		for (int i = 0; i < BOM_GPIOS_COUNT; i++) {
			gpio_port = rvp_config->bom_gpios_config[i].port;
			if (!device_is_ready(gpio_port)) {
				LOG_DBG("RVP_ID: Initializing %s",
					SAFE_DEV_NAME(gpio_port));
				ret = device_init(gpio_port);
				if (ret) {
					LOG_ERR("RVP_ID: Cannot init driver '%s': %d (BOM GPIO %d)",
						SAFE_DEV_NAME(gpio_port), ret,
						i);
				}
			}
		}
		for (int i = 0; i < FAB_GPIOS_COUNT; i++) {
			gpio_port = rvp_config->fab_gpios_config[i].port;
			if (!device_is_ready(gpio_port)) {
				LOG_DBG("RVP_ID: Initializing %s",
					SAFE_DEV_NAME(gpio_port));
				ret = device_init(gpio_port);
				if (ret) {
					LOG_ERR("RVP_ID: Cannot init driver '%s': %d (FAB GPIO %d)",
						SAFE_DEV_NAME(gpio_port), ret,
						i);
				}
			}
		}
		for (int i = 0; i < BOARD_GPIOS_COUNT; i++) {
			gpio_port = rvp_config->board_gpios_config[i].port;
			if (!device_is_ready(gpio_port)) {
				LOG_DBG("RVP_ID: Initializing %s",
					SAFE_DEV_NAME(gpio_port));
				ret = device_init(gpio_port);
				if (ret) {
					LOG_ERR("RVP_ID: Cannot init driver '%s': %d (BOARD GPIO %d)",
						SAFE_DEV_NAME(gpio_port), ret,
						i);
				}
			}
		}
		if (rvp_config->handler != NULL)
			rvp_config->handler();
	}
}
AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE(ap_power_state_callback,
				     AP_POWER_STATE_S5);

static int rvp_board_id_init(const struct device *dev)
{
	rvp_config = dev->config;
	return 0;
}
#else
static int rvp_board_id_init(const struct device *dev)
{
	rvp_config = dev->config;
	return 0;
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

#if DT_NODE_HAS_PROP(DT_DRV_INST(0), handler)
extern void DT_STRING_TOKEN(DT_DRV_INST(0), handler)(void);
#endif

static const struct rvp_board_id_config rvp_board_id_cfg = {
#if DT_NODE_HAS_PROP(DT_DRV_INST(0), bom_gpios)
	.bom_gpios_config =
		(const struct gpio_dt_spec[]){
			FOREACH_RVP_GPIOS_ELEM(0, bom_gpios) },
#else
	.bom_gpios_config = NULL,
#endif
#if DT_NODE_HAS_PROP(DT_DRV_INST(0), fab_gpios)
	.fab_gpios_config =
		(const struct gpio_dt_spec[]){
			FOREACH_RVP_GPIOS_ELEM(0, fab_gpios) },
#else
	.fab_gpios_config = NULL,
#endif
	.board_gpios_config =
		(const struct gpio_dt_spec[]){
			FOREACH_RVP_GPIOS_ELEM(0, board_gpios) },
	.handler = DT_INST_STRING_TOKEN_OR(0, handler, NULL),
};

static int initialize_device(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	int ret;

	if (!(dev->flags & DEVICE_FLAG_INIT_DEFERRED)) {
		/* Not deferred. Let the kernel initialize the device. */
		return 0;
	}

	ret = device_init(dev);

	if (ret) {
		LOG_ERR("RVP_ID: Cannot start driver: %d", ret);
	}
	return ret;
}

SYS_INIT(initialize_device, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

DEVICE_DT_INST_DEFINE(0, rvp_board_id_init, NULL, NULL, &rvp_board_id_cfg,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
