/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific SHI module for Chrome EC */

#ifndef __CROS_EC_SHI_CHIP_H_
#define __CROS_EC_SHI_CHIP_H_

#include "host_command.h"

/**
 * Called when the NSS level changes, signalling the start of a SHI
 * transaction.
 *
 * @param signal	GPIO signal that changed
 */
void shi_cs_event(enum gpio_signal signal);
#ifdef NPCX_SHI_V2
void shi_cs_gpio_int(enum gpio_signal signal);
#endif

enum ec_status shi_get_protocol_info(struct host_cmd_handler_args *args);

#endif /* __CROS_EC_SHI_CHIP_H_ */
