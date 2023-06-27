/* Copyright 2022 Google LLC
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_INCLUDE_VCMP_MOCK_H
#define __EMUL_INCLUDE_VCMP_MOCK_H

#include <zephyr/device.h>

/*
 * Manually trigger the vcmp handler
 *
 * @param dev pointer to the vcmp emulator device
 */
void vcmp_mock_trigger(const struct device *dev);

#endif /*__EMUL_INCLUDE_VCMP_MOCK_H */
