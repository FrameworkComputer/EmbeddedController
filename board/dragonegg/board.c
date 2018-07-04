/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg board-specific configuration */

#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "intc.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "uart.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* SPI devices */
/* TODO(b/110880394): Fill out correctly (SPI FLASH) */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
