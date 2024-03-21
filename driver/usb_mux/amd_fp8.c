/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP8 USB/DP/USB4 Mux.
 */

#include "amd_fp8.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
#include "queue.h"
#include "timer.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define AMD_FP8_MUX_COUNT 3
#define AMD_FP8_MUX_RESCHEDULE_DELAY_MS 10

#define AMD_FP8_MUX_HELPER(id)                                         \
	{                                                              \
		.mux = { .i2c_port = I2C_PORT_BY_DEV(id),              \
			 .i2c_addr_flags = DT_REG_ADDR(id),            \
			 .usb_port = USB_MUX_PORT(id) },               \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),    \
		.fixed_state = DT_PROP_OR(id, fixed, USB_PD_MUX_NONE), \
	},

struct amd_fp8_mux_state {
	bool xbar_ready;
	mux_state_t current_state;

	/* Keep track of if we're waiting for the mux to change state. */
	bool in_progress;
	mux_state_t next_state;

	const struct usb_mux mux;
	/* Each mux has two ports, but only port-0 is currently used. */
	uint8_t port;
	const struct gpio_dt_spec irq_gpio;

	/*
	 * We can have muxes that are connected to type-A ports. These still
	 * need to be configured to enable full USB3 speeds, but exist outside
	 * of the normal mux flow. fixed_state allows us to configure these
	 * muxes internally in the driver.
	 */
	const mux_state_t fixed_state;
};

static struct amd_fp8_mux_state amd_fp8_muxes[] = { DT_FOREACH_STATUS_OKAY(
	AMD_FP8_USB_MUX_COMPAT, AMD_FP8_MUX_HELPER) };

/*
 * We might need to check all muxes, even those that aren't in use, to clear the
 * interrupt line. Ensure that all muxes are defined in the devicetree.
 */
BUILD_ASSERT(ARRAY_SIZE(amd_fp8_muxes) == AMD_FP8_MUX_COUNT);

static K_MUTEX_DEFINE(amd_fp8_lock);

static int amd_fp8_init(void)
{
	k_mutex_init(&amd_fp8_lock);
	return EC_SUCCESS;
}
SYS_INIT(amd_fp8_init, POST_KERNEL, 50);

static int amd_fp8_mux_read(const struct amd_fp8_mux_state *amd_mux,
			    uint8_t command, uint8_t *buf, uint8_t len)
{
	return i2c_xfer(amd_mux->mux.i2c_port, amd_mux->mux.i2c_addr_flags,
			&command, 1, buf, len);
}

/*
 * Lookup the corresponding internal mux state from a generic mux struct.
 */
static struct amd_fp8_mux_state *
amd_fp8_lookup_mux_state(const struct usb_mux *me)
{
	for (size_t i = 0; i < ARRAY_SIZE(amd_fp8_muxes); i++) {
		if (amd_fp8_muxes[i].mux.usb_port == me->usb_port)
			return &amd_fp8_muxes[i];
	}

	return NULL;
}

/* Different mux/port combinations might only support USB3/DP, not USB4/TBT. */
static bool amd_fp8_mux_supports_usb4(uint8_t addr, uint8_t port)
{
	if (port != 0)
		return false;

	switch (addr) {
	case AMD_FP8_MUX_ADDR0:
	case AMD_FP8_MUX_ADDR1:
		return true;

	case AMD_FP8_MUX_ADDR2:
		return false;

	default:
		return false;
	}
}

static int amd_fp8_set_mux_unsafe(struct amd_fp8_mux_state *amd_mux,
				  mux_state_t mux_state)
{
	uint8_t ctrl = 0;
	uint8_t payload[AMD_FP8_WRITE1_USB4_LEN];
	uint8_t payload_len = AMD_FP8_WRITE1_USB3_LEN;
	int rv;
	const uint8_t i2c_addr = amd_mux->mux.i2c_addr_flags;
	const int usb_port = amd_mux->mux.usb_port;

	if (amd_mux->port != 0) {
		CPRINTSUSB("AMD FP8(%02x): Invalid mux port", i2c_addr);
		return EC_ERROR_INVAL;
	}

	/*
	 * Validate that the mux is ready and isn't already processing a
	 * command.
	 */
	if (!amd_mux->xbar_ready) {
		/* Mux is only active in S0. */
		if (power_get_state() == POWER_S0)
			CPRINTSUSB("AMD FP8(%02x): skip mux set xbar not ready",
				   i2c_addr);
		return EC_ERROR_BUSY;
	}

	if (amd_mux->in_progress) {
		CPRINTSUSB("AMD FP8(%02x): skip mux set, in progress",
			   i2c_addr);
		return EC_ERROR_BUSY;
	}

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	/* Set our port. */
	payload[AMD_FP8_MUX_WRITE1_INDEX_BYTE] = amd_mux->port;

	if (mux_state == USB_PD_MUX_NONE) {
		ctrl = AMD_FP8_CONTROL_SAFE;
	} else if ((mux_state & USB_PD_MUX_USB_ENABLED) &&
		   (mux_state & USB_PD_MUX_DP_ENABLED)) {
		ctrl = AMD_FP8_CONTROL_DOCK;
	} else if (mux_state & USB_PD_MUX_USB_ENABLED) {
		ctrl = AMD_FP8_CONTROL_USB;
	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		ctrl = AMD_FP8_CONTROL_DP;
	} else {
		if (!amd_fp8_mux_supports_usb4(i2c_addr, amd_mux->port)) {
			CPRINTSUSB("AMD FP8(%02x): unhandled mux_state %x",
				   i2c_addr, mux_state);
			return EC_ERROR_INVAL;
		}
	}

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ctrl |= AMD_FP8_MUX_W1_CTRL_FLIP;

	/* TODO(b/276335130): Add Data reset request */

	/* These are only relevant for USB4/TBT supporting muxes. */
	if (amd_fp8_mux_supports_usb4(i2c_addr, amd_mux->port)) {
		payload_len = AMD_FP8_WRITE1_USB4_LEN;

		if (pd_get_data_role(usb_port) == PD_ROLE_UFP)
			ctrl |= AMD_FP8_MUX_W1_CTRL_UFP;

		/* Enable TBT3/USB4. */
		payload[AMD_FP8_MUX_WRITE1_CABLE_BYTE] = 0;
		if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED)
			payload[AMD_FP8_MUX_WRITE1_CABLE_BYTE] |=
				AMD_FP8_MUX_W1_CABLE_TBT3;
		else if (mux_state & USB_PD_MUX_USB4_ENABLED)
			payload[AMD_FP8_MUX_WRITE1_CABLE_BYTE] |=
				AMD_FP8_MUX_W1_CABLE_USB4;

		/* TODO(b/276335130): Add Cable information */
		payload[AMD_FP8_MUX_WRITE1_VER_BYTE] = 0;

		if (pd_is_connected(usb_port))
			payload[AMD_FP8_MUX_WRITE1_SPEED_BYTE] =
				AMD_FP8_MUX_W1_SPEED_TC;
		else
			payload[AMD_FP8_MUX_WRITE1_SPEED_BYTE] = 0;
	}

	payload[AMD_FP8_MUX_WRITE1_CONTROL_BYTE] = ctrl;

	rv = i2c_xfer(amd_mux->mux.i2c_port, i2c_addr, payload, payload_len,
		      NULL, 0);
	if (rv) {
		CPRINTSUSB("AMD FP8(%02x): I2C mux error, %d", usb_port, rv);
		return rv;
	}

	/* Save our mux state now that it's passed error checks */
	amd_mux->next_state = mux_state;
	amd_mux->in_progress = true;

	return rv;
}

static int amd_fp8_read_int_status(const struct amd_fp8_mux_state *amd_mux,
				   uint8_t *status)
{
	return amd_fp8_mux_read(amd_mux, AMD_FP8_MUX_READ5_CODE, status, 1);
}

static int amd_fp8_read_status(const struct amd_fp8_mux_state *amd_mux,
			       uint8_t *status, uint8_t *port0_status,
			       uint8_t *port1_status)
{
	uint8_t data[3];
	int rv;

	rv = amd_fp8_mux_read(amd_mux, AMD_FP8_MUX_READ3_CODE, data,
			      sizeof(data));
	if (rv)
		return rv;

	*status = data[0];
	*port0_status = data[1];
	*port1_status = data[2];
	return rv;
}

static int amd_fp8_read_mailbox(const struct amd_fp8_mux_state *amd_mux,
				uint8_t *data, uint8_t len)
{
	if (len != AMD_FP8_MUX_READ4_LEN)
		return EC_ERROR_INVAL;

	return amd_fp8_mux_read(amd_mux, AMD_FP8_MUX_READ4_CODE, data, len);
}

static void amd_fp8_check_error_state(struct amd_fp8_mux_state *amd_mux,
				      uint8_t int_status, uint8_t mux_status)
{
	if (int_status & AMD_FP8_MUX_R5_ERROR_INT)
		CPRINTSUSB("AMD FP8(%02x): I2C error",
			   amd_mux->mux.i2c_addr_flags);

	if (mux_status & AMD_FP8_MUX_R3_STATUS_ERROR)
		CPRINTSUSB("AMD FP8(%02x): error", amd_mux->mux.i2c_addr_flags);
}

static void amd_fp8_check_xbar_state(struct amd_fp8_mux_state *amd_mux,
				     uint8_t int_status, uint8_t mux_status)
{
	if (!(int_status & AMD_FP8_MUX_R5_XBAR_INT))
		return;

	amd_mux->xbar_ready = mux_status & AMD_FP8_MUX_R3_STATUS_READY;
}

static void amd_fp8_check_command_state(struct amd_fp8_mux_state *amd_mux,
					uint8_t int_status, uint8_t port_status)
{
	enum amd_fp8_command_status cmd_status =
		(port_status & AMD_FP8_MUX_R3_PORT0_STATUS_MASK) >> 6;

	/* Only check on muxes that were doing something. */
	if (!amd_mux->in_progress)
		return;

	if (!(int_status & AMD_FP8_MUX_R5_COMMAND_INT ||
	      int_status & AMD_FP8_MUX_R5_ERROR_INT))
		return;

	/* Acknowledge if mux set was successful. */
	if (cmd_status == AMD_FP8_COMMAND_STATUS_COMPLETE)
		usb_mux_set_ack_complete(amd_mux->mux.usb_port);
	else if (cmd_status == AMD_FP8_COMMAND_STATUS_IN_PROGRESS)
		CPRINTSUSB("AMD FP8(%02x): Command running, target state: %x",
			   amd_mux->mux.i2c_addr_flags, amd_mux->next_state);
	else
		CPRINTSUSB("AMD FP8(%02x): Command failed, target state: %x",
			   amd_mux->mux.i2c_addr_flags, amd_mux->next_state);

	amd_mux->in_progress = false;
	amd_mux->current_state = amd_mux->next_state;
	amd_mux->next_state = USB_PD_MUX_NONE;
}

static void amd_fp8_update_fixed_states(void)
{
	int rv;

	for (size_t i = 0; i < ARRAY_SIZE(amd_fp8_muxes); i++) {
		struct amd_fp8_mux_state *amd_mux = &amd_fp8_muxes[i];

		/* Set fixed mux state. */
		if (!amd_mux->xbar_ready)
			continue;

		if (amd_mux->in_progress)
			continue;

		if (amd_mux->fixed_state == USB_PD_MUX_NONE)
			continue;

		if (amd_mux->current_state == amd_mux->fixed_state)
			continue;

		rv = amd_fp8_set_mux_unsafe(amd_mux, amd_mux->fixed_state);
		if (rv)
			CPRINTSUSB("AMD FP8(%02x): fixed mux state fail %x, %d",
				   amd_mux->mux.i2c_addr_flags,
				   amd_mux->fixed_state, rv);
	}
}

static void amd_fp8_mux_interrupt_handler_call(int delay_ms);

void amd_fp8_mux_interrupt_handler(void)
{
	uint8_t int_status;
	uint8_t port0_status;
	uint8_t unused;
	uint8_t mux_status;
	int rv;
	int int_asserted = 0;

	k_mutex_lock(&amd_fp8_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(amd_fp8_muxes); i++) {
		struct amd_fp8_mux_state *amd_mux = &amd_fp8_muxes[i];

		rv = amd_fp8_read_int_status(amd_mux, &int_status);
		if (rv) {
			CPRINTSUSB("AMD FP8(%02x): Failed to get int status %d",
				   amd_mux->mux.i2c_addr_flags, rv);
			continue;
		}

		/* Check mux status register. */
		if (int_status & AMD_FP8_MUX_R5_COMMAND_INT ||
		    int_status & AMD_FP8_MUX_R5_ERROR_INT ||
		    int_status & AMD_FP8_MUX_R5_XBAR_INT) {
			rv = amd_fp8_read_status(amd_mux, &mux_status,
						 &port0_status, &unused);
			if (rv) {
				CPRINTSUSB("AMD FP8(%02x): port status fail %d",
					   amd_mux->mux.i2c_addr_flags, rv);
				continue;
			}

			amd_fp8_check_error_state(amd_mux, int_status,
						  mux_status);
			amd_fp8_check_xbar_state(amd_mux, int_status,
						 mux_status);
			amd_fp8_check_command_state(amd_mux, int_status,
						    port0_status);
		}

		/* Check and clear APU mailbox. */
		if (int_status & AMD_FP8_MUX_R5_MAIL_INT) {
			uint8_t data[AMD_FP8_MUX_READ4_LEN];

			rv = amd_fp8_read_mailbox(amd_mux, data, sizeof(data));
			if (rv)
				CPRINTSUSB("AMD FP8(%02x): mailbox fail %d",
					   amd_mux->mux.i2c_addr_flags, rv);
		}

		/*
		 * If the interrupt line is de-asserted then we've handled
		 * all interrupts.
		 */
		int_asserted = gpio_pin_get_dt(&amd_mux->irq_gpio);
		if (!int_asserted)
			break;
	}

	amd_fp8_update_fixed_states();

	k_mutex_unlock(&amd_fp8_lock);

	if (int_asserted) {
		/*
		 * Have more interrupts, give other tasks some time to run
		 * first.
		 */
		CPRINTSUSB("AMD FP8: More interrupts, rescheduling");
		amd_fp8_mux_interrupt_handler_call(
			AMD_FP8_MUX_RESCHEDULE_DELAY_MS);
	}
}
DECLARE_DEFERRED(amd_fp8_mux_interrupt_handler);

static void amd_fp8_mux_interrupt_handler_call(int delay_ms)
{
	hook_call_deferred(&amd_fp8_mux_interrupt_handler_data,
			   delay_ms * MSEC);
}

void amd_fp8_mux_interrupt(enum gpio_signal signal)
{
	amd_fp8_mux_interrupt_handler_call(0);
}

static int amd_fp8_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required)
{
	struct amd_fp8_mux_state *amd_mux;
	int rv;

	/* This driver does require host command ACKs */
	*ack_required = true;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		/* We won't be getting any ACK's from the SoC */
		*ack_required = false;
		return (mux_state == USB_PD_MUX_NONE) ? EC_SUCCESS :
							EC_ERROR_NOT_POWERED;
	}

	k_mutex_lock(&amd_fp8_lock, K_FOREVER);
	amd_mux = amd_fp8_lookup_mux_state(me);

	/* Find the corresponding internal mux state. */
	if (!amd_mux) {
		CPRINTSUSB("C%d: Unsupported mux", me->usb_port);
		k_mutex_unlock(&amd_fp8_lock);
		return EC_ERROR_INVAL;
	}

	rv = amd_fp8_set_mux_unsafe(amd_mux, mux_state);
	k_mutex_unlock(&amd_fp8_lock);
	return rv;
}

static int amd_fp8_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	struct amd_fp8_mux_state *amd_mux = amd_fp8_lookup_mux_state(me);

	if (!amd_mux)
		return EC_ERROR_INVAL;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		*mux_state = USB_PD_MUX_NONE;
	else
		*mux_state = amd_mux->current_state;

	return EC_SUCCESS;
}

/*
 * The FP8 USB mux will not be ready for writing until *sometime* after S0.
 */
static void amd_fp8_chipset_resume(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(amd_fp8_muxes); i++) {
		mux_state_t state = amd_fp8_muxes[i].current_state;
		/* Queue this up for the mux task */
		usb_mux_set(i, state,
			    (state == USB_PD_MUX_NONE ? USB_SWITCH_DISCONNECT :
							USB_SWITCH_CONNECT),
			    (state & USB_PD_MUX_POLARITY_INVERTED));
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, amd_fp8_chipset_resume, HOOK_PRIO_DEFAULT);

static int amd_fp8_chipset_reset(const struct usb_mux *me)
{
	if (!chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON))
		return EC_SUCCESS;

	for (size_t i = 0; i < ARRAY_SIZE(amd_fp8_muxes); i++) {
		amd_fp8_muxes[i].in_progress = false;
		amd_fp8_muxes[i].current_state = USB_PD_MUX_NONE;
	}

	/* TODO(b/276335130): Will this double-resume? Filter by me->usb_port */
	amd_fp8_chipset_resume();
	return EC_SUCCESS;
}

const struct usb_mux_driver amd_fp8_usb_mux_driver = {
	.set = &amd_fp8_set_mux,
	.get = &amd_fp8_get_mux,
	.chipset_reset = &amd_fp8_chipset_reset
};
