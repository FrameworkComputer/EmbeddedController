/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_I2C_I2C_H
#define ZEPHYR_CHROME_I2C_I2C_H

#include <device.h>
#include <devicetree.h>

#ifdef CONFIG_PLATFORM_EC_I2C
/*
 * Define the i2c_ports enum for Ztests only right now. In full builds this
 * will clash with the definitions in config_chip-npcx7.h. Once we've migrated
 * away from platform/ec/chip/... files we can remove this guard.
 */
#if defined(CONFIG_ZTEST) && DT_NODE_EXISTS(DT_PATH(named_i2c_ports))
#define I2C_PORT(id) DT_CAT(I2C_, id)
#define I2C_PORT_WITH_COMMA(id) I2C_PORT(id),
enum i2c_ports {
DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), I2C_PORT_WITH_COMMA)
I2C_PORT_COUNT
};
#define NAMED_I2C(name) I2C_PORT(DT_PATH(named_i2c_ports, name))
#endif /* CONFIG_ZTEST && named_i2c_ports */
#endif /* CONFIG_PLATFORM_EC_I2C */

/**
 * @brief Adaptation of platform/ec's port IDs which map a port/bus to a device.
 *
 * This function should be implemented per chip and should map the enum value
 * defined for the chip for encoding each valid port/bus combination. For
 * example, the npcx chip defines the port/bus combinations NPCX_I2C_PORT* under
 * chip/npcx/registers-npcx7.h.
 *
 * Thus, the npcx shim should implement this function to map the enum values
 * to the correct devicetree device.
 *
 * @param port The port to get the device for.
 * @return Pointer to the device struct or {@code NULL} if none are available.
 */
const struct device *i2c_get_device_for_port(const int port);

#endif /* ZEPHYR_CHROME_I2C_I2C_H */
