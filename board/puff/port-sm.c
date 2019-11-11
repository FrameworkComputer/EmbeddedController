/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

struct port_states {
	/* PORT_*n masks correspond to bits in this field */
	uint8_t bitfield;
	/* If 1, type C is RP_1A5 (otherwise assumed to be RP_3A0) */
	uint8_t c_low_power;
	/* If 1, front ports are current-limited */
	uint8_t front_a_limited;
};

#define PORTMASK_FRONT_A0 0
#define PORTMASK_FRONT_A1 1
#define PORTMASK_REAR_A0 2
#define PORTMASK_REAR_A1 3
#define PORTMASK_REAR_A2 4
#define PORTMASK_HDMI0 5
#define PORTMASK_HDMI1 6
#define PORTMASK_TYPEC 7

#define PORT_FRONT_A0 (1 << PORTMASK_FRONT_A0)
#define PORT_FRONT_A1 (1 << PORTMASK_FRONT_A1)
#define PORT_REAR_A0 (1 << PORTMASK_REAR_A0)
#define PORT_REAR_A1 (1 << PORTMASK_REAR_A1)
#define PORT_REAR_A2 (1 << PORTMASK_REAR_A2)
#define PORT_HDMI0 (1 << PORTMASK_HDMI0)
#define PORT_HDMI1 (1 << PORTMASK_HDMI1)
#define PORT_TYPEC (1 << PORTMASK_TYPEC)
#define PORT_ENABLED(id) (!!(states->bitfield & (PORT_##id)))

#define PWR_FRONT_HIGH 1603
#define PWR_FRONT_LOW 963
#define PWR_REAR 1075
#define PWR_HDMI 562
#define PWR_C_HIGH 3740
#define PWR_C_LOW 2090

/*
 * Calculate the amount of power (in mA) available on the 5V rail.
 *
 * If negative, the system is at risk of browning out.
 */
int compute_headroom(const struct port_states *states)
{
	int headroom = 10000 - 1335; /* Capacity less base load */

	headroom -= PWR_HDMI * (PORT_ENABLED(HDMI0) + PORT_ENABLED(HDMI1));
	headroom -= PWR_REAR * (PORT_ENABLED(REAR_A0) + PORT_ENABLED(REAR_A1) +
				PORT_ENABLED(REAR_A2));

	switch (PORT_ENABLED(FRONT_A0) + PORT_ENABLED(FRONT_A1)) {
	case 2:
		headroom -= PWR_FRONT_LOW + (states->front_a_limited ?
						     PWR_FRONT_LOW :
						     PWR_FRONT_HIGH);
		break;
	case 1:
		headroom -= states->front_a_limited ? PWR_FRONT_LOW :
						      PWR_FRONT_HIGH;
		break;
	default:
		break;
	}

	if (PORT_ENABLED(TYPEC))
		headroom -= states->c_low_power ? PWR_C_LOW : PWR_C_HIGH;

	return headroom;
}

/*
 * Update states to stay within the 5V rail power budget.
 *
 * Only the current limits (c_low_power and front_a_limited) are effective.
 *
 * The goal here is to ensure that any single state change from what we set
 * (specifically, something being plugged into a port) does not exceed the 5V
 * power budget.
 */
void update_port_state(struct port_states *states)
{
	int headroom = compute_headroom(states);

	if (!PORT_ENABLED(TYPEC)) {
		/*
		 * USB-C not in use, prefer to adjust it. We may still need
		 * to limit front port power.
		 *
		 * We want to run the front type-A ports at high power, and they
		 * may be limited so we need to account for the extra power
		 * we may be allowing the front ports to draw.
		 */
		if (headroom >
		    (PWR_C_HIGH + (PWR_FRONT_HIGH - PWR_FRONT_LOW))) {
			states->front_a_limited = 0;
			states->c_low_power = 0;
		} else {
			states->front_a_limited =
				headroom <
				(PWR_C_LOW + (PWR_FRONT_HIGH - PWR_FRONT_LOW));
			states->c_low_power = 1;
		}
	} else {
		/*
		 * USB-C is in use, prefer to drop front port limits.
		 * Pessimistically Assume C is currently in low-power mode.
		 */
		if (headroom > (PWR_C_HIGH - PWR_C_LOW + PWR_FRONT_HIGH)) {
			/* Can still go full power */
			states->front_a_limited = 0;
			states->c_low_power = 0;
		} else if (headroom >
			   (PWR_C_HIGH - PWR_C_LOW + PWR_FRONT_LOW)) {
			/* Reducing front allows C to go to full power */
			states->front_a_limited = 1;
			states->c_low_power = 0;
		} else {
			/* Must reduce both */
			states->front_a_limited = 1;
			states->c_low_power = 1;
		}
	}
}
