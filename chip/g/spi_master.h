/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INCLUDE_SPI_MASTER_H
#define __CROS_EC_INCLUDE_SPI_MASTER_H

#include "spi.h"

void configure_spi0_passthrough(int enable);
void set_spi_clock_mode(int port, enum spi_clock_mode mode);

#endif
