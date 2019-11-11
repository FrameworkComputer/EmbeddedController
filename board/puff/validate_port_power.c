/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <klee/klee.h>

#include "port-sm.c"

int main(void)
{
	uint8_t bitfield = klee_range(0, 256, "port_bitmask");
	uint8_t c_low_power = klee_range(0, 2, "c_low_power");
	uint8_t front_a_limited = klee_range(0, 2, "front_a_limited");
	struct port_states states = {
		.bitfield = bitfield,
		.c_low_power = c_low_power,
		.front_a_limited = front_a_limited
	};
	/*
	 * Assume illegal states with no headroom cannot be reached in the first
	 * place.
	 */
	klee_assume(compute_headroom(&states) >= 0);

	update_port_state(&states);

	/*
	 * Plug something into an unused port and ensure we still have
	 * non-negative headroom.
	 */
	uint8_t enable_port = (uint8_t)klee_range(0, 8, "enable_port");
	uint8_t enable_mask = 1 << enable_port;

	klee_assume((states.bitfield & enable_mask) == 0);
	states.bitfield |= enable_mask;
	klee_assert(compute_headroom(&states) >= 0);

	return 0;
}
