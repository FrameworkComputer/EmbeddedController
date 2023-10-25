/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_CEC_H
#define __CROS_EC_ZEPHYR_CEC_H

#include <zephyr/devicetree.h>

#define CEC_COMPAT cros_ec_cec
#define CEC_NODE DT_INST(0, CEC_COMPAT)

#define CEC_PORT_ID(node_id) DT_REG_ADDR(node_id)

#define CEC_NODE_ID_WITH_COMMA(node_id) node_id,
/* clang-format off */
enum cec_port {
	DT_FOREACH_CHILD(CEC_NODE, CEC_NODE_ID_WITH_COMMA)
	CEC_PORT_COUNT
};
/* clang-format on */

#endif /* __CROS_EC_ZEPHYR_CEC_H */
