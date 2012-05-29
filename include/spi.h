/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI interface for Chrome EC */

#ifndef __CROS_EC_SPI_H
#define __CROS_EC_SPI_H

/* Enable / disable the SPI port.  When the port is disabled, all its I/O lines
 * are high-Z so the EC won't interfere with other devices on the SPI bus. */
int spi_enable(int enable);

/* Issue a SPI transaction.  Assumes SPI port has already been enabled.
 * Transmits <txlen> bytes from <txdata>, throwing away the corresponding
 * received data, then transmits <rxlen> dummy bytes, saving the received data
 * in <rxdata>. */
int spi_transaction(const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen);

#endif  /* __CROS_EC_SPI_H */
