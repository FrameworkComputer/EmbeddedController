/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stddef.h>

#include <gpio_signal.h>

struct gpio_dt_spec;
void amd_stb_dump_finish(void);
void amd_stb_dump_init(const struct gpio_dt_spec *int_out,
		       const struct gpio_dt_spec *int_in);
bool amd_stb_dump_in_progress(void);
void amd_stb_dump_interrupt(enum gpio_signal signal);
void amd_stb_dump_trigger(void);
