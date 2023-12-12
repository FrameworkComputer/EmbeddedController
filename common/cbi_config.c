/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_config.h"

#if defined(CONFIG_CBI_FLASH)
const struct cbi_storage_config_t *cbi_config = &flash_cbi_config;
#elif defined(CONFIG_CBI_EEPROM)
const struct cbi_storage_config_t *cbi_config = &eeprom_cbi_config;
#elif defined(CONFIG_CBI_GPIO)
const struct cbi_storage_config_t *cbi_config = &gpio_cbi_config;
#else
#error "CBI storage config not found."
#endif
