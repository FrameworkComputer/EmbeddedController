/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_IOEXPANDER_TCA64XXA_H_
#define __CROS_EC_DRIVER_IOEXPANDER_TCA64XXA_H_

/* io-expander driver specific flag bit for tca6416a */
#define IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6416A IOEX_FLAGS_CUSTOM_BIT(24)
/* io-expander driver specific flag bit for tca6424a */
#define IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6424A IOEX_FLAGS_CUSTOM_BIT(25)

#define TCA64XXA_FLAG_VER_MASK		GENMASK(2, 1)
#define TCA64XXA_FLAG_VER_OFFSET	0

#define TCA64XXA_REG_INPUT		0
#define TCA64XXA_REG_OUTPUT		1
#define TCA64XXA_REG_POLARITY_INV	2
#define TCA64XXA_REG_CONF		3

#define TCA64XXA_DEFAULT_OUTPUT		0xFF
#define TCA64XXA_DEFAULT_POLARITY_INV	0x00
#define TCA64XXA_DEFAULT_CONF		0xFF

extern const struct ioexpander_drv tca64xxa_ioexpander_drv;

#endif /* __CROS_EC_DRIVER_IOEXPANDER_TCA64XXA_H_ */
