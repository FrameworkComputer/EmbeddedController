/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trace dump module */

#ifndef __CROS_EC_TRACE_H
#define __CROS_EC_TRACE_H

#ifdef EMU_BUILD
/*
 * Register trace dump handler for emulator. Trace dump is printed to stderr
 * when SIGUSR2 is received.
 */
void task_register_tracedump(void);

/* Dump current stack trace */
void task_dump_trace(void);
#else
static inline void task_register_tracedump(void) { }
static inline void task_dump_trace(void) { }
#endif

#endif  /* __CROS_EC_TRACE_H */
