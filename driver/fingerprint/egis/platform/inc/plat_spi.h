/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_SPI_H_
#define __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_SPI_H_

#include "spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Issues a SPI transaction. Assumes SPI port has already been enabled.
 *
 * Transmits @p tx_len bytes from @p tx_addr, throwing away the corresponding
 * received data, then transmits @p rx_len bytes, saving the received data in @p
 * rx_buf.
 *
 * @param[in] tx_addr Pointer to the transmit buffer containing the data to be
 * sent.
 * @param[in] tx_len Length of the transmit buffer in bytes.
 * @param[out] rx_buf Pointer to the receive buffer where received data will be
 * stored.
 * @param[in] rx_len Length of the receive buffer in bytes.
 *
 * @return 0 on success.
 * @return A value from @ref ec_error_list enum.
 */
int periphery_spi_write_read(uint8_t *tx_addr, uint32_t tx_len, uint8_t *rx_buf,
			     uint32_t rx_len);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_SPI_H_ */
