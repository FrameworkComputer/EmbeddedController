/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_SHIM_USBC_UTIL


/*
 * Enable interrupt from the `irq` property of an instance's node.
 *
 * @param inst: instance number
 */
#define BC12_GPIO_ENABLE_INTERRUPT(inst)				\
	IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, irq),			\
		   (gpio_enable_dt_interrupt(				\
			GPIO_INT_FROM_NODE(DT_INST_PHANDLE(inst, irq)));\
		   ) \
		  )

/*
 * Get the port number from a child of `named-usbc-port` node.
 *
 * @param id: node id
 */
#define USBC_PORT(id) DT_REG_ADDR(DT_PARENT(id))

/*
 * Get the port number from a child of `named-usbc-port` node.
 *
 * @param inst: instance number of the node
 */
#define USBC_PORT_FROM_INST(inst) USBC_PORT(DT_DRV_INST(inst))


#endif /* __CROS_EC_ZEPHYR_SHIM_USBC_UTIL */
