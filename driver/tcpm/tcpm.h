/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management - common header for TCPM drivers */

#ifndef __CROS_EC_USB_PD_TCPM_TCPM_H
#define __CROS_EC_USB_PD_TCPM_TCPM_H

#include "i2c.h"
#include "usb_pd_tcpm.h"

extern const struct tcpc_config_t tcpc_config[];

/* I2C wrapper functions - get I2C port / slave addr from config struct. */
static inline int tcpc_write(int port, int reg, int val)
{
	return i2c_write8(tcpc_config[port].i2c_host_port,
			  tcpc_config[port].i2c_slave_addr,
			  reg, val);
}

static inline int tcpc_write16(int port, int reg, int val)
{
	return i2c_write16(tcpc_config[port].i2c_host_port,
			   tcpc_config[port].i2c_slave_addr,
			   reg, val);
}

static inline int tcpc_read(int port, int reg, int *val)
{
	return i2c_read8(tcpc_config[port].i2c_host_port,
			 tcpc_config[port].i2c_slave_addr,
			 reg, val);
}

static inline int tcpc_read16(int port, int reg, int *val)
{
	return i2c_read16(tcpc_config[port].i2c_host_port,
			  tcpc_config[port].i2c_slave_addr,
			  reg, val);
}

static inline int tcpc_xfer(int port,
			    const uint8_t *out, int out_size,
			    uint8_t *in, int in_size,
			    int flags)
{
	return i2c_xfer(tcpc_config[port].i2c_host_port,
			tcpc_config[port].i2c_slave_addr,
			out, out_size,
			in, in_size,
			flags);
}

static inline void tcpc_lock(int port, int lock)
{
	i2c_lock(tcpc_config[port].i2c_host_port, lock);
}

#endif
