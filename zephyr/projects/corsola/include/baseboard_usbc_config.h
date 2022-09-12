/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */

#ifndef __CROS_EC_BASEBOARD_USBC_CONFIG_H
#define __CROS_EC_BASEBOARD_USBC_CONFIG_H

#ifdef CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1718S
#define GPIO_EN_USB_C1_SINK RT1718S_GPIO1
#define GPIO_EN_USB_C1_SOURCE RT1718S_GPIO2
#define GPIO_EN_USB_C1_FRS RT1718S_GPIO3
#endif

void ppc_interrupt(enum gpio_signal signal);

/* USB-A ports */
enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_COUNT };

/* USB-C ports */
enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

/**
 * Is the port fine to be muxed its DisplayPort lines?
 *
 * Only one port can be muxed to DisplayPort at a time.
 *
 * @param port	Port number of TCPC.
 * @return	1 is fine; 0 is bad as other port is already muxed;
 */
int corsola_is_dp_muxable(int port);

#endif /* __CROS_EC_BASEBOARD_USBC_CONFIG_H */
