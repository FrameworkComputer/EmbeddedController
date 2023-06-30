/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __HOST_BSP_SERVICE_H
#define __HOST_BSP_SERVICE_H
/**
 * @brief host Services APIs
 * @defgroup host interface host Service APIs
 * @{
 */
#include <errno.h>

#include <heci.h>

/**
 * @brief
 * callback function being called to handle protocol message
 * @retval 0 If successful.
 */
typedef int (*bsp_msg_handler_f)(uint32_t drbl);

/**
 * @brief
 * add function cb in local-host service task
 * to handle host messages.
 * @retval 0 If successful.
 */
int host_protocol_register(uint8_t protocol_id, bsp_msg_handler_f handler);
/**
 * @}
 */

#endif /* __HOST_BSP_SERVICE_H */
