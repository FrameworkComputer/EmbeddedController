/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_NON_DSX_ESPI_H__
#define __X86_NON_DSX_ESPI_H__

/**
 * @brief Configure eSPI channels for Non Deep Sleep well platforms
 */
void ndsx_espi_configure(void);

/**
 * @brief Return Virtual wire value

 * @retval Actual Virtual Wire value or 0 on failure
 */
uint8_t vw_get_level(enum espi_vwire_signal signal);

#endif /* __NDSX_ESPI_H__ */
