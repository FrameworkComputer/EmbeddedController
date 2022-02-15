/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include <drivers/eeprom.h>
#include "gpio.h"
#include "write_protect.h"

#if DT_NODE_EXISTS(DT_NODELABEL(cbi_eeprom))
#define CBI_EEPROM_DEV DEVICE_DT_GET(DT_NODELABEL(cbi_eeprom))

#ifdef CONFIG_PLATFORM_EC_EEPROM_CBI_WP
void cbi_latch_eeprom_wp(void)
{
	cprints(CC_SYSTEM, "CBI WP latched");
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_wp), 1);
}
#endif /* CONFIG_PLATFORM_EC_EEPROM_CBI_WP */

static int eeprom_load(uint8_t offset, uint8_t *data, int len)
{
	return eeprom_read(CBI_EEPROM_DEV, offset, data, len);
}

static int eeprom_is_write_protected(void)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_BYPASS_CBI_EEPROM_WP_CHECK))
		return 0;

	return write_protect_is_asserted();
}

static int eeprom_store(uint8_t *cbi)
{
	return eeprom_write(CBI_EEPROM_DEV, 0, cbi,
			    ((struct cbi_header *)cbi)->total_size);
}

static const struct cbi_storage_driver eeprom_drv = {
	.store = eeprom_store,
	.load = eeprom_load,
	.is_protected = eeprom_is_write_protected,
};

const struct cbi_storage_config_t cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_EEPROM,
	.drv = &eeprom_drv,
};
#endif
