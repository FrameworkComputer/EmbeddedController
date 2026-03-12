
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_FPSENSOR_FPSENSOR_LED_H_
#define __CROS_EC_FPSENSOR_FPSENSOR_LED_H_

#include "ec_commands.h"

#include <cstdint>

namespace fp_led
{

#if defined(CONFIG_PLATFORM_EC_FP_LED)
void update_mode(uint32_t sensor_mode);
void update_match(bool success);
#else
inline void update_mode(uint32_t sensor_mode)
{
}
inline void update_match(bool success)
{
}
#endif

} // namespace fp_led

#endif /*__CROS_EC_FPSENSOR_FPSENSOR_LED_H_*/
