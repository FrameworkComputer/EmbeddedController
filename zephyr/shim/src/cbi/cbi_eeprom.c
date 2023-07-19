/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_config.h"
#include "console.h"
#include "cros_board_info.h"
#include "write_protect.h"

#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/gpio.h>

#define CBI_EEPROM_NODE DT_NODELABEL(cbi_eeprom)

BUILD_ASSERT(DT_NODE_EXISTS(CBI_EEPROM_NODE), "cbi_eeprom node not defined");

#ifdef CONFIG_PLATFORM_EC_EEPROM_CBI_WP
#if !DT_NODE_EXISTS(DT_ALIAS(gpio_cbi_wp))
#error gpio_cbi_wp alias has to point to the CBI WP output pin.
#endif

void cbi_latch_eeprom_wp(void)
{
	cprints(CC_SYSTEM, "CBI WP latched");
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_cbi_wp), 1);
}
#endif /* CONFIG_PLATFORM_EC_EEPROM_CBI_WP */

test_mockable_static int eeprom_load(uint8_t offset, uint8_t *data, int len)
{
	const struct device *dev;

	dev = DEVICE_DT_GET(CBI_EEPROM_NODE);

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	return eeprom_read(dev, offset, data, len);
}

static int eeprom_is_write_protected(void)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_BYPASS_CBI_EEPROM_WP_CHECK)) {
		return 0;
	}

#ifdef CONFIG_PLATFORM_EC_EEPROM_CBI_WP
	return gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_cbi_wp));
#else
	/* GSC controlled write protect */
	return write_protect_is_asserted();
#endif
}

static int eeprom_store(uint8_t *cbi)
{
	const struct device *dev;

	dev = DEVICE_DT_GET(CBI_EEPROM_NODE);

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	return eeprom_write(dev, 0, cbi,
			    ((struct cbi_header *)cbi)->total_size);
}

static const struct cbi_storage_driver eeprom_drv = {
	.store = eeprom_store,
	.load = eeprom_load,
	.is_protected = eeprom_is_write_protected,
};

const struct cbi_storage_config_t eeprom_cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_EEPROM,
	.drv = &eeprom_drv,
};
