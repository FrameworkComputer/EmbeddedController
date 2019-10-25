/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager */

#include "atomic.h"
#include "anx74xx.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "ps8xxx.h"
#include "task.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#ifdef CONFIG_USB_PD_DECODE_SOP
static int vconn_en[CONFIG_USB_PD_PORT_COUNT];
static int rx_en[CONFIG_USB_PD_PORT_COUNT];
#endif
static int tcpc_vbus[CONFIG_USB_PD_PORT_COUNT];

/* Save the selected rp value */
static int selected_rp[CONFIG_USB_PD_PORT_COUNT];


#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
int tcpc_addr_write(int port, int i2c_addr, int reg, int val)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_write8(tcpc_config[port].i2c_info.port,
			i2c_addr, reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_write16(int port, int reg, int val)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_write16(tcpc_config[port].i2c_info.port,
			 tcpc_config[port].i2c_info.addr_flags,
			 reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_addr_read(int port, int i2c_addr, int reg, int *val)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_read8(tcpc_config[port].i2c_info.port,
		       i2c_addr, reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_read16(int port, int reg, int *val)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_read16(tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_read_block(int port, int reg, uint8_t *in, int size)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_read_block(tcpc_config[port].i2c_info.port,
			    tcpc_config[port].i2c_info.addr_flags,
			    reg, in, size);

	pd_device_accessed(port);
	return rv;
}

int tcpc_write_block(int port, int reg, const uint8_t *out, int size)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_write_block(tcpc_config[port].i2c_info.port,
			     tcpc_config[port].i2c_info.addr_flags,
			     reg, out, size);

	pd_device_accessed(port);
	return rv;
}

int tcpc_xfer(int port, const uint8_t *out, int out_size,
			uint8_t *in, int in_size)
{
	int rv;
	/* Dispatching to tcpc_xfer_unlocked reduces code size growth. */
	tcpc_lock(port, 1);
	rv = tcpc_xfer_unlocked(port, out, out_size, in, in_size,
				I2C_XFER_SINGLE);
	tcpc_lock(port, 0);
	return rv;
}

int tcpc_xfer_unlocked(int port, const uint8_t *out, int out_size,
			    uint8_t *in, int in_size, int flags)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_xfer_unlocked(tcpc_config[port].i2c_info.port,
			       tcpc_config[port].i2c_info.addr_flags,
			       out, out_size, in, in_size, flags);

	pd_device_accessed(port);
	return rv;
}
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

static int init_alert_mask(int port)
{
	int rv;
	uint16_t mask;

	/*
	 * Create mask of alert events that will cause the TCPC to
	 * signal the TCPM via the Alert# gpio line.
	 */
	mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
		| TCPC_REG_ALERT_POWER_STATUS
#endif
		;
	/* Set the alert mask in TCPC */
	rv = tcpc_write16(port, TCPC_REG_ALERT_MASK, mask);

	if (IS_ENABLED(CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP)) {
		if (rv)
			return rv;

		/* Sink FRS allowed */
		mask = TCPC_REG_ALERT_EXT_SNK_FRS;
		rv = tcpc_write(port, TCPC_REG_ALERT_EXT, mask);
	}
	return rv;
}

static int clear_alert_mask(int port)
{
	return tcpc_write16(port, TCPC_REG_ALERT_MASK, 0);
}

static int init_power_status_mask(int port)
{
	uint8_t mask;
	int rv;

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	mask = TCPC_REG_POWER_STATUS_VBUS_PRES;
#else
	mask = 0;
#endif
	rv = tcpc_write(port, TCPC_REG_POWER_STATUS_MASK , mask);

	return rv;
}

static int clear_power_status_mask(int port)
{
	return tcpc_write(port, TCPC_REG_POWER_STATUS_MASK, 0);
}

int tcpci_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	int status;
	int rv;

	rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);

	/* If tcpc read fails, return error and CC as open */
	if (rv) {
		*cc1 = TYPEC_CC_VOLT_OPEN;
		*cc2 = TYPEC_CC_VOLT_OPEN;
		return rv;
	}

	*cc1 = TCPC_REG_CC_STATUS_CC1(status);
	*cc2 = TCPC_REG_CC_STATUS_CC2(status);

	/*
	 * If status is not open, then OR in termination to convert to
	 * enum tcpc_cc_voltage_status.
	 */
	if (*cc1 != TYPEC_CC_VOLT_OPEN)
		*cc1 |= TCPC_REG_CC_STATUS_TERM(status) << 2;
	if (*cc2 != TYPEC_CC_VOLT_OPEN)
		*cc2 |= TCPC_REG_CC_STATUS_TERM(status) << 2;

	return rv;
}

static int tcpci_tcpm_get_power_status(int port, int *status)
{
	return tcpc_read(port, TCPC_REG_POWER_STATUS, status);
}

int tcpci_tcpm_select_rp_value(int port, int rp)
{
	selected_rp[port] = rp;
	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
void tcpci_tcpc_discharge_vbus(int port, int enable)
{
	int reg;

	if (tcpc_read(port, TCPC_REG_POWER_CTRL, &reg))
		return;

	if (enable)
		reg |= TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	else
		reg &= ~TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;

	tcpc_write(port, TCPC_REG_POWER_CTRL, reg);
}
#endif

static int set_role_ctrl(int port, int toggle, int rp, int pull)
{
	return tcpc_write(port, TCPC_REG_ROLE_CTRL,
			  TCPC_REG_ROLE_CTRL_SET(toggle, rp, pull, pull));
}

int tcpci_tcpm_set_cc(int port, int pull)
{
	/* Set manual control, and set both CC lines to the same pull */
	return set_role_ctrl(port, 0, selected_rp[port], pull);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
int tcpci_tcpc_drp_toggle(int port)
{
	int rv;

	/* Set auto drp toggle */
	rv = set_role_ctrl(port, 1, TYPEC_RP_USB, TYPEC_CC_RD);

	/* Set Look4Connection command */
	rv |= tcpc_write(port, TCPC_REG_COMMAND,
			 TCPC_REG_COMMAND_LOOK4CONNECTION);

	return rv;
}
#endif

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
int tcpci_enter_low_power_mode(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}
#endif

int tcpci_tcpm_set_polarity(int port, int polarity)
{
	return tcpc_write(port, TCPC_REG_TCPC_CTRL,
			  TCPC_REG_TCPC_CTRL_SET(polarity));
}

#ifdef CONFIG_USBC_PPC
int tcpci_tcpm_set_snk_ctrl(int port, int enable)
{
	int cmd = enable ? TCPC_REG_COMMAND_SNK_CTRL_HIGH :
		TCPC_REG_COMMAND_SNK_CTRL_LOW;

	return tcpc_write(port, TCPC_REG_COMMAND, cmd);
}

int tcpci_tcpm_set_src_ctrl(int port, int enable)
{
	int cmd = enable ? TCPC_REG_COMMAND_SRC_CTRL_HIGH :
		TCPC_REG_COMMAND_SRC_CTRL_LOW;

	return tcpc_write(port, TCPC_REG_COMMAND, cmd);
}
#endif

int tcpci_tcpm_set_vconn(int port, int enable)
{
	int reg, rv;

	rv = tcpc_read(port, TCPC_REG_POWER_CTRL, &reg);
	if (rv)
		return rv;

#ifdef CONFIG_USB_PD_DECODE_SOP
	/* save vconn */
	vconn_en[port] = enable;

	if (rx_en[port]) {
		int detect_sop_en = TCPC_REG_RX_DETECT_SOP_HRST_MASK;

		if (enable) {
			detect_sop_en =
				TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK;
		}

		tcpc_write(port, TCPC_REG_RX_DETECT, detect_sop_en);
	}
#endif
	reg &= ~TCPC_REG_POWER_CTRL_VCONN(1);
	reg |= TCPC_REG_POWER_CTRL_VCONN(enable);
	return tcpc_write(port, TCPC_REG_POWER_CTRL, reg);
}

int tcpci_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_write(port, TCPC_REG_MSG_HDR_INFO,
			  TCPC_REG_MSG_HDR_INFO_SET(data_role, power_role));
}

static int tcpm_alert_status(int port, int *alert)
{
	/* Read TCPC Alert register */
	return tcpc_read16(port, TCPC_REG_ALERT, alert);
}

static int tcpm_alert_ext_status(int port, int *alert_ext)
{
	/* Read TCPC Extended Alert register */
	return tcpc_read(port, TCPC_REG_ALERT_EXT, alert_ext);
}

int tcpci_tcpm_set_rx_enable(int port, int enable)
{
	int detect_sop_en = 0;

	if (enable) {
		detect_sop_en = TCPC_REG_RX_DETECT_SOP_HRST_MASK;

#ifdef CONFIG_USB_PD_DECODE_SOP
		/* save rx_on */
		rx_en[port] = enable;

		/*
		 * Only the VCONN Source is allowed to communicate
		 * with the Cable Plugs.
		 */

		if (vconn_en[port])
			detect_sop_en =
				TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK;
#endif
	}

	/* If enable, then set RX detect for SOP and HRST */
	return tcpc_write(port, TCPC_REG_RX_DETECT, detect_sop_en);
}

#ifdef CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP
void tcpci_tcpc_fast_role_swap_enable(int port, int enable)
{
	int reg;

	if (tcpc_read(port, TCPC_REG_POWER_CTRL, &reg))
		return;

	if (enable)
		reg |= TCPC_REG_POWER_CTRL_FRS_ENABLE;
	else
		reg &= ~TCPC_REG_POWER_CTRL_FRS_ENABLE;

	tcpc_write(port, TCPC_REG_POWER_CTRL, reg);

	board_tcpc_fast_role_swap_enable(port, enable);
}
#endif

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
int tcpci_tcpm_get_vbus_level(int port)
{
	return tcpc_vbus[port];
}
#endif

struct cached_tcpm_message {
	uint32_t header;
	uint32_t payload[7];
};

int tcpci_tcpm_get_message_raw(int port, uint32_t *payload, int *head)
{
	int rv, cnt, reg = TCPC_REG_RX_DATA;
#ifdef CONFIG_USB_PD_DECODE_SOP
	int frm;
#endif

	rv = tcpc_read(port, TCPC_REG_RX_BYTE_CNT, &cnt);

	/* RX_BYTE_CNT includes 3 bytes for frame type and header */
	if (rv != EC_SUCCESS || cnt < 3) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}
	cnt -= 3;
	if (cnt > member_size(struct cached_tcpm_message, payload)) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}

#ifdef CONFIG_USB_PD_DECODE_SOP
	rv = tcpc_read(port, TCPC_REG_RX_BUF_FRAME_TYPE, &frm);
	if (rv != EC_SUCCESS) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}
#endif

	rv = tcpc_read16(port, TCPC_REG_RX_HDR, (int *)head);

#ifdef CONFIG_USB_PD_DECODE_SOP
	/* Encode message address in bits 31 to 28 */
	*head &= 0x0000ffff;
	*head |= PD_HEADER_SOP(frm & 7);
#endif
	if (rv == EC_SUCCESS && cnt > 0) {
		tcpc_read_block(port, reg, (uint8_t *)payload, cnt);
	}

clear:
	/* Read complete, clear RX status alert bit */
	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_STATUS);

	return rv;
}

/* Cache depth needs to be power of 2 */
#define CACHE_DEPTH BIT(2)
#define CACHE_DEPTH_MASK (CACHE_DEPTH - 1)

struct queue {
	/*
	 * Head points to the index of the first empty slot to put a new RX
	 * message. Must be masked before used in lookup.
	 */
	uint32_t head;
	/*
	 * Tail points to the index of the first message for the PD task to
	 * consume. Must be masked before used in lookup.
	 */
	uint32_t tail;
	struct cached_tcpm_message buffer[CACHE_DEPTH];
};
static struct queue cached_messages[CONFIG_USB_PD_PORT_COUNT];

/* Note this method can be called from an interrupt context. */
int tcpm_enqueue_message(const int port)
{
	int rv;
	struct queue *const q = &cached_messages[port];
	struct cached_tcpm_message *const head =
		&q->buffer[q->head & CACHE_DEPTH_MASK];

	if (q->head - q->tail == CACHE_DEPTH) {
		CPRINTS("C%d RX EC Buffer full!", port);
		return EC_ERROR_OVERFLOW;
	}

	/* Blank any old message, just in case. */
	memset(head, 0, sizeof(*head));
	/* Call the raw driver without caching */
	rv = tcpc_config[port].drv->get_message_raw(port, head->payload,
						    &head->header);
	if (rv) {
		CPRINTS("C%d: Could not retrieve RX message (%d)", port, rv);
		return rv;
	}

	/* Increment atomically to ensure get_message_raw happens-before */
	atomic_add(&q->head, 1);

	/* Wake PD task up so it can process incoming RX messages */
	task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE, 0);

	return EC_SUCCESS;
}

int tcpm_has_pending_message(const int port)
{
	const struct queue *const q = &cached_messages[port];

	return q->head != q->tail;
}

int tcpm_dequeue_message(const int port, uint32_t *const payload,
			 int *const header)
{
	struct queue *const q = &cached_messages[port];
	struct cached_tcpm_message *const tail =
		&q->buffer[q->tail & CACHE_DEPTH_MASK];

	if (!tcpm_has_pending_message(port)) {
		CPRINTS("C%d No message in RX buffer!", port);
		return EC_ERROR_BUSY;
	}

	/* Copy cache data in to parameters */
	*header = tail->header;
	memcpy(payload, tail->payload, sizeof(tail->payload));

	/* Increment atomically to ensure memcpy happens-before */
	atomic_add(&q->tail, 1);

	return EC_SUCCESS;
}

void tcpm_clear_pending_messages(int port)
{
	struct queue *const q = &cached_messages[port];

	q->tail = q->head;
}

int tcpci_tcpm_transmit(int port, enum tcpm_transmit_type type,
			uint16_t header, const uint32_t *data)
{
	int reg = TCPC_REG_TX_DATA;
	int rv, cnt = 4*PD_HEADER_CNT(header);

	/* If not SOP* transmission, just write to the transmit register */
	if (type >= NUM_SOP_STAR_TYPES) {
		/*
		 * Per TCPCI spec, do not specify retry (although the TCPC
		 * should ignore retry field for these 3 types).
		 */
		return tcpc_write(port, TCPC_REG_TRANSMIT,
			TCPC_REG_TRANSMIT_SET_WITHOUT_RETRY(type));
	}

	/* TX_BYTE_CNT includes extra bytes for message header */
	rv = tcpc_write(port, TCPC_REG_TX_BYTE_CNT, cnt + sizeof(header));

	rv |= tcpc_write16(port, TCPC_REG_TX_HDR, header);

	/* If tcpc read fails, return error */
	if (rv)
		return rv;

	if (cnt > 0) {
		rv = tcpc_write_block(port, reg, (const uint8_t *)data, cnt);

		/* If tcpc read fails, return error */
		if (rv)
			return rv;
	}

	/*
	 * On receiving a received message on SOP, protocol layer
	 * discards the pending  SOP messages queued for transmission.
	 * But it doesn't do the same for SOP' message. So retry is
	 * assigned to 0 to avoid multiple transmission.
	 */
	return tcpc_write(port, TCPC_REG_TRANSMIT,
				(type == TCPC_TX_SOP_PRIME) ?
				TCPC_REG_TRANSMIT_SET_WITHOUT_RETRY(type) :
				TCPC_REG_TRANSMIT_SET_WITH_RETRY(type));
}

#ifndef CONFIG_USB_PD_TCPC_LOW_POWER
/*
 * Returns true if TCPC has reset based on reading mask registers. Only need to
 * check this if the TCPC low power mode (LPM) code isn't compiled in because
 * LPM will automatically reset the device when the TCPC exits LPM.
 */
static int register_mask_reset(int port)
{
	int mask;

	mask = 0;
	tcpc_read16(port, TCPC_REG_ALERT_MASK, &mask);
	if (mask == TCPC_REG_ALERT_MASK_ALL)
		return 1;

	mask = 0;
	tcpc_read(port, TCPC_REG_POWER_STATUS_MASK, &mask);
	if (mask == TCPC_REG_POWER_STATUS_MASK_ALL)
		return 1;

	return 0;
}
#endif

/*
 * Don't let the TCPC try to pull from the RX buffer forever. We typical only
 * have 1 or 2 messages waiting.
 */
#define MAX_ALLOW_FAILED_RX_READS 10

void tcpci_tcpc_alert(int port)
{
	int status = 0;
	int alert_ext = 0;
	int failed_attempts;
	uint32_t pd_event = 0;

	/* Read the Alert register from the TCPC */
	tcpm_alert_status(port, &status);

	/* Get Extended Alert register if needed */
	if (status & TCPC_REG_ALERT_ALERT_EXT)
		tcpm_alert_ext_status(port, &alert_ext);

	/*
	 * Check for TX complete first b/c PD state machine waits on TX
	 * completion events. This will send an event to the PD tasks
	 * immediately
	 */
	if (status & TCPC_REG_ALERT_TX_COMPLETE)
		pd_transmit_complete(port, status & TCPC_REG_ALERT_TX_SUCCESS ?
					   TCPC_TX_COMPLETE_SUCCESS :
					   TCPC_TX_COMPLETE_FAILED);

	/* Pull all RX messages from TCPC into EC memory */
	failed_attempts = 0;
	while (status & TCPC_REG_ALERT_RX_STATUS) {
		if (tcpm_enqueue_message(port))
			++failed_attempts;
		if (tcpm_alert_status(port, &status))
			++failed_attempts;

		/* Ensure we don't loop endlessly */
		if (failed_attempts >= MAX_ALLOW_FAILED_RX_READS) {
			CPRINTS("C%d Cannot consume RX buffer after %d failed attempts!",
				port, failed_attempts);
			/*
			 * The port is in a bad state, we don't want to consume
			 * all EC resources so suspend the port for a little
			 * while.
			 */
			pd_set_suspend(port, 1);
			pd_deferred_resume(port);
			return;
		}
	}

	/* Clear all pending alert bits */
	if (status)
		tcpc_write16(port, TCPC_REG_ALERT, status);

	if (status & TCPC_REG_ALERT_CC_STATUS) {
		/* CC status changed, wake task */
		pd_event |= PD_EVENT_CC;
	}
	if (status & TCPC_REG_ALERT_POWER_STATUS) {
		int reg = 0;
		/* Read Power Status register */
		tcpci_tcpm_get_power_status(port, &reg);
		/* Update VBUS status */
		tcpc_vbus[port] = reg &
			TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
#if defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) && defined(CONFIG_USB_CHARGER)
		/* Update charge manager with new VBUS state */
		usb_charger_vbus_change(port, tcpc_vbus[port]);
		pd_event |= TASK_EVENT_WAKE;
#endif /* CONFIG_USB_PD_VBUS_DETECT_TCPC && CONFIG_USB_CHARGER */
	}
	if (status & TCPC_REG_ALERT_RX_HARD_RST) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		pd_event |= TASK_EVENT_WAKE;
	}

	if (IS_ENABLED(CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP)
	    && (alert_ext & TCPC_REG_ALERT_EXT_SNK_FRS))
		pd_got_frs_signal(port);

#ifndef CONFIG_USB_PD_TCPC_LOW_POWER
	/*
	 * Check registers to see if we can tell that the TCPC has reset. If
	 * so, perform a tcpc_init. This only needs to happen for devices that
	 * don't support low power mode as the transition from low power mode
	 * will automatically reset the device.
	 */
	if (register_mask_reset(port))
		pd_event |= PD_EVENT_TCPC_RESET;
#endif

	/*
	 * Wait until all possible TCPC accesses in this function are complete
	 * prior to setting events and/or waking the pd task. When the PD
	 * task is woken and runs (which will happen during I2C transactions in
	 * this function), the pd task may put the TCPC into low power mode and
	 * the next I2C transaction to the TCPC will cause it to wake again.
	 */
	if (pd_event)
		task_set_event(PD_PORT_TO_TASK_ID(port), pd_event, 0);
}

/*
 * This call will wake up the TCPC if it is in low power mode upon accessing the
 * i2c bus (but the pd state machine should put it back into low power mode).
 *
 * Once it's called, the chip info will be stored in cache, which can be
 * accessed by tcpm_get_chip_info without worrying about chip states.
 */
int tcpci_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 **chip_info)
{
	static struct ec_response_pd_chip_info_v1
		info[CONFIG_USB_PD_PORT_COUNT];
	struct ec_response_pd_chip_info_v1 *i;
	int error;
	int val;

	if (port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_ERROR_INVAL;

	i = &info[port];

	/* If chip_info is NULL, chip info will be stored in cache and can be
	 * read later by another call. */
	if (chip_info)
		*chip_info = i;

	/* If already cached && live data is not asked, return cached value */
	if (i->vendor_id && !live)
		return EC_SUCCESS;

	error = tcpc_read16(port, TCPC_REG_VENDOR_ID, &val);
	if (error)
		return error;
	i->vendor_id = val;

	error = tcpc_read16(port, TCPC_REG_PRODUCT_ID, &val);
	if (error)
		return error;
	i->product_id = val;

	error = tcpc_read16(port, TCPC_REG_BCD_DEV, &val);
	if (error)
		return error;
	i->device_id = val;

	/*
	 * This varies chip to chip; more specific driver code is expected to
	 * override this value if it can.
	 */
	i->fw_version_number = -1;

	return EC_SUCCESS;
}

/*
 * Dissociate from the TCPC.
 */

int tcpci_tcpm_release(int port)
{
	int error;

	error = clear_alert_mask(port);
	if (error)
		return error;
	error = clear_power_status_mask(port);
	if (error)
		return error;
	/* Clear pending interrupts */
	error = tcpc_write16(port, TCPC_REG_ALERT, 0xffff);
	if (error)
		return error;

	return EC_SUCCESS;
}

/*
 * On TCPC i2c failure, make 30 tries (at least 300ms) before giving up
 * in order to allow the TCPC time to boot / reset.
 */
#define TCPM_INIT_TRIES 30

int tcpci_tcpm_init(int port)
{
	int error;
	int power_status;
	int tries = TCPM_INIT_TRIES;

	while (1) {
		error = tcpc_read(port, TCPC_REG_POWER_STATUS, &power_status);
		/*
		 * If read succeeds and the uninitialized bit is clear, then
		 * initialization is complete, clear all alert bits and write
		 * the initial alert mask.
		 */
		if (!error && !(power_status & TCPC_REG_POWER_STATUS_UNINIT))
			break;
		if (--tries <= 0)
			return error ? error : EC_ERROR_TIMEOUT;
		msleep(10);
	}

	tcpc_write16(port, TCPC_REG_ALERT, 0xffff);
	/* Initialize power_status_mask */
	init_power_status_mask(port);
	/* Update VBUS status */
	tcpc_vbus[port] = power_status &
			TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
#if defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) && defined(CONFIG_USB_CHARGER)
	/*
	 * Set Vbus change now in case the TCPC doesn't send a power status
	 * changed interrupt for it later.
	 */
	usb_charger_vbus_change(port, tcpc_vbus[port]);
#endif
	error = init_alert_mask(port);
	if (error)
		return error;

	/* Read chip info here when we know the chip is awake. */
	tcpm_get_chip_info(port, 1, NULL);

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPM_MUX

/*
 * When the TCPC/MUX device is only used for the MUX, we need to initialize it
 * via mux init because tcpc_init won't run for the device. This is borrowed
 * from tcpc_init.
 */
int tcpci_tcpm_mux_init(int port)
{
	int error;
	int power_status;
	int tries = TCPM_INIT_TRIES;

	/* If this MUX is also the TCPC, then skip init */
	if (!(usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC))
		return EC_SUCCESS;

	/* Wait for the device to exit low power state */
	while (1) {
		error = mux_read(port, TCPC_REG_POWER_STATUS, &power_status);
		/*
		 * If read succeeds and the uninitialized bit is clear, then
		 * initialization is complete.
		 */
		if (!error && !(power_status & TCPC_REG_POWER_STATUS_UNINIT))
			break;
		if (--tries <= 0)
			return error ? error : EC_ERROR_TIMEOUT;
		msleep(10);
	}

	/* Turn off all alerts and acknowledge any pending IRQ */
	error = mux_write16(port, TCPC_REG_ALERT_MASK, 0);
	error |= mux_write16(port, TCPC_REG_ALERT, 0xffff);

	return error ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}

int tcpci_tcpm_mux_enter_low_power(int port)
{
	/* If this MUX is also the TCPC, then skip low power */
	if (!(usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC))
		return EC_SUCCESS;

	return mux_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

int tcpci_tcpm_mux_set(int port, mux_state_t mux_state)
{
	int reg = 0;
	int rv;

	/* Parameter is port only */
	rv = mux_read(port, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	reg &= ~(TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK |
		 TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED);
	if (mux_state & MUX_USB_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	if (mux_state & MUX_DP_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP;
	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;

	/* Parameter is port only */
	return mux_write(port, TCPC_REG_CONFIG_STD_OUTPUT, reg);
}

/* Reads control register and updates mux_state accordingly */
int tcpci_tcpm_mux_get(int port, mux_state_t *mux_state)
{
	int reg = 0;
	int rv;

	*mux_state = 0;

	/* Parameter is port only */
	rv = mux_read(port, TCPC_REG_CONFIG_STD_OUTPUT, &reg);

	if (rv != EC_SUCCESS)
		return rv;

	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP)
		*mux_state |= MUX_DP_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver tcpci_tcpm_usb_mux_driver = {
	.init = &tcpci_tcpm_mux_init,
	.set = &tcpci_tcpm_mux_set,
	.get = &tcpci_tcpm_mux_get,
	.enter_low_power_mode = &tcpci_tcpm_mux_enter_low_power,
};

#endif /* CONFIG_USB_PD_TCPM_MUX */

const struct tcpm_drv tcpci_tcpm_drv = {
	.init			= &tcpci_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &tcpci_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
};
