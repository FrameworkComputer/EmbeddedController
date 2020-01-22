/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Deltaur board-specific configuration */

#include "common.h"
#include "system.h"
#include "task.h"
#include "spi.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/*
 * TODO(b/148160415): Evaluate if hibernate wake signals are needed
 */
const enum gpio_signal hibernate_wake_pins[] = {};
const int hibernate_wake_pins_used;

/******************************************************************************/
/* SPI devices */
/*
 * TODO(b/148160415): Evaluate if external flash needs to be set here
 */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
