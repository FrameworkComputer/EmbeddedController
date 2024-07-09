/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CBI_CONFIG_H
#define __CROS_EC_CBI_CONFIG_H

#include "cros_board_info.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_CBI_FLASH)
extern const struct cbi_storage_config_t flash_cbi_config;
#endif

#if defined(CONFIG_CBI_EEPROM)
extern const struct cbi_storage_config_t eeprom_cbi_config;
#endif

#if defined(CONFIG_CBI_GPIO)
extern const struct cbi_storage_config_t gpio_cbi_config;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CBI_CONFIG_H */
