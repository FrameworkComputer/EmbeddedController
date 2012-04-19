/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Symbols from linker definitions
 */

#ifndef __CROS_EC_LINK_DEFS_H
#define __CROS_EC_LINK_DEFS_H

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"

extern const struct console_command __cmds[];
extern const struct console_command __cmds_end[];

extern const struct hook_data __hooks_init[];
extern const struct hook_data __hooks_init_end[];
extern const struct hook_data __hooks_freq_change[];
extern const struct hook_data __hooks_freq_change_end[];

extern const struct host_command __hcmds[];
extern const struct host_command __hcmds_end[];

extern const struct irq_priority __irqprio[];
extern const struct irq_priority __irqprio_end[];

#endif /* __CROS_EC_LINK_DEFS_H */
