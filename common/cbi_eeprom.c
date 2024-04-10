/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Support Cros Board Info EEPROM */

#include "cbi_config.h"
#include "console.h"
#include "cros_board_info.h"
#include "gpio.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "write_protect.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ##args)

/*
 * We allow EEPROMs with page size of 8 or 16. Use 8 to be the most compatible.
 * This causes a little more overhead for writes, but we are not writing to the
 * EEPROM outside of the factory process.
 */
#define EEPROM_PAGE_WRITE_SIZE 8
#define EEPROM_PAGE_WRITE_MS 5

static int eeprom_read(uint8_t offset, uint8_t *data, int len)
{
	return i2c_read_block(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS, offset,
			      data, len);
}

static int eeprom_is_write_protected(void)
{
	if (IS_ENABLED(CONFIG_BYPASS_CBI_EEPROM_WP_CHECK))
		return 0;

	return write_protect_is_asserted();
}

static int eeprom_write(uint8_t *cbi)
{
	uint8_t *p = cbi;
	int rest = ((struct cbi_header *)p)->total_size;

	while (rest > 0) {
		int size = MIN(EEPROM_PAGE_WRITE_SIZE, rest);
		int rv;

		rv = i2c_write_block(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
				     p - cbi, p, size);
		if (rv) {
			CPRINTS("Failed to write for %d", rv);
			return rv;
		}
		/* Wait for internal write cycle completion */
		crec_msleep(EEPROM_PAGE_WRITE_MS);
		p += size;
		rest -= size;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_EEPROM_CBI_WP
void cbi_latch_eeprom_wp(void)
{
	CPRINTS("WP latched");
	gpio_set_level(GPIO_EC_CBI_WP, 1);
}
#endif /* CONFIG_EEPROM_CBI_WP */

const struct cbi_storage_driver eeprom_drv = {
	.store = eeprom_write,
	.load = eeprom_read,
	.is_protected = eeprom_is_write_protected,
};

const struct cbi_storage_config_t eeprom_cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_EEPROM,
	.drv = &eeprom_drv,
};
