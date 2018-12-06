/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch board-specific configuration */

#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "lid_switch.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "uart.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
