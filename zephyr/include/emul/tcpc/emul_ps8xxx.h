/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for PS8xxx emulator
 */

#ifndef __EMUL_PS8XXX_H
#define __EMUL_PS8XXX_H

#include <drivers/emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/**
 * @brief PS8xxx emulator backend API
 * @defgroup ps8xxx_emul PS8xxx emulator
 * @{
 *
 * PS8xxx emulator is extension for the TCPCI emulator. It is able to emulate
 * PS8805 and PS8815 devices. It registers "hidden" I2C devices with the I2C
 * emulation controller.
 * Application may alter emulator state:
 *
 * - call @ref ps8xxx_emul_set_product_id to select which device is emulated
 *   (PS8805 or PS8815)
 * - call @ref ps8xxx_emul_get_tcpci to get TCPCI emulator pointer that is used
 *   as base for PS8xxx emulator. The pointer can be used in tcpci_emul_*
 *   functions.
 * - call @ref ps8xxx_emul_get_i2c_emul to get "hidden" I2C device (port 0, 1
 *   or GPIO)
 * - call @ref ps8xxx_emul_set_chip_rev to set PS8805 chip revision
 * - call @ref ps8xxx_emul_set_hw_rev to set PS8815 HW revision
 * - call @ref ps8xxx_emul_set_gpio_ctrl to set GPIO control register
 */

/** Types of "hidden" I2C devices */
enum ps8xxx_emul_port {
	PS8XXX_EMUL_PORT_0,
	PS8XXX_EMUL_PORT_1,
	PS8XXX_EMUL_PORT_GPIO,
	PS8XXX_EMUL_PORT_INVAL,
};

/* For now all devices supported by this emulator has the same FW rev reg */
#define PS8XXX_REG_FW_REV		0x82

/**
 * @brief Get pointer to specific "hidden" I2C device
 *
 * @param emul Pointer to PS8xxx emulator
 * @param port Select which "hidden" I2C device should be obtained
 *
 * @return NULL if given "hidden" I2C device cannot be found
 * @return pointer to "hidden" I2C device
 */
struct i2c_emul *ps8xxx_emul_get_i2c_emul(const struct emul *emul,
					  enum ps8xxx_emul_port port);

/**
 * @brief Get pointer to TCPCI emulator that is base for PS8xxx emulator
 *
 * @param emul Pointer to PS8xxx emulator
 *
 * @return pointer to TCPCI emulator
 */
const struct emul *ps8xxx_emul_get_tcpci(const struct emul *emul);

/**
 * @brief Set value of chip revision register on PS8805
 *
 * @param emul Pointer to PS8xxx emulator
 * @param chip_rev Value to be set
 */
void ps8xxx_emul_set_chip_rev(const struct emul *emul, uint8_t chip_rev);

/**
 * @brief Set value of HW revision register on PS8815
 *
 * @param emul Pointer to PS8xxx emulator
 * @param hw_rev Value to be set
 */
void ps8xxx_emul_set_hw_rev(const struct emul *emul, uint16_t hw_rev);

/**
 * @brief Set value of GPIO control register
 *
 * @param emul Pointer to PS8xxx emulator
 * @param gpio_ctrl Value to be set
 */
void ps8xxx_emul_set_gpio_ctrl(const struct emul *emul, uint8_t gpio_ctrl);

/**
 * @brief Get value of GPIO control register
 *
 * @param emul Pointer to PS8xxx emulator
 *
 * @return Value of GPIO control register
 */
uint8_t ps8xxx_emul_get_gpio_ctrl(const struct emul *emul);

/**
 * @brief Get value of mux usb DCI configuration register
 *
 * @param emul Pointer to PS8xxx emulator
 *
 * @return Value of mux usb DCI configuration register
 */
uint8_t ps8xxx_emul_get_dci_cfg(const struct emul *emul);

/**
 * @brief Set product ID of emulated PS8xxx device. This change behaviour
 *        of emulator to mimic that device. Currently supported are PS8805 and
 *        PS8815
 *
 * @param emul Pointer to PS8xxx emulator
 * @param product_id Value to be set
 *
 * @return 0 on success
 * @return -EINVAL when unsupported product ID is selected
 */
int ps8xxx_emul_set_product_id(const struct emul *emul, uint16_t product_id);

/**
 * @brief Get product ID of emulated PS8xxx device
 *
 * @param emul Pointer to PS8xxx emulator
 *
 * @return Product ID of emulated PS8xxx device
 */
uint16_t ps8xxx_emul_get_product_id(const struct emul *emul);

/**
 * @}
 */

#endif /* __EMUL_PS8XXX */
