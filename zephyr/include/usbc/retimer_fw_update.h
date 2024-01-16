/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Header file to expose retimer firmware update APIs.
 */

#ifndef __ZEPHYR_INCLUDE_USBC_PD_RETIMER_FW_UPDATE_H
#define __ZEPHYR_INCLUDE_USBC_PD_RETIMER_FW_UPDATE_H

/*
 * @brief Get result of last retimer firmware update operation.
 */
int usb_retimer_fw_update_get_result(void);

/*
 *@brief Process retimer firmware update operation.
 */
void usb_retimer_fw_update_process_op(int port, int op);

#endif /* __ZEPHYR_INCLUDE_USBC_PD_RETIMER_FW_UPDATE_H */
