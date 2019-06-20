/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_H
#define __CROS_EC_USB_PD_TCPM_H

#include "ec_commands.h"
#include "i2c.h"

/* Default retry count for transmitting */
#define PD_RETRY_COUNT 3

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT  (100*MSEC)

/* Number of valid Transmit Types */
#define NUM_XMIT_TYPES (TCPC_TX_SOP_DEBUG_PRIME_PRIME + 1)

/* Detected resistor values of port partner */
enum tcpc_cc_voltage_status {
	TYPEC_CC_VOLT_OPEN = 0,
	TYPEC_CC_VOLT_RA = 1,	  /* Port partner is applying Ra */
	TYPEC_CC_VOLT_RD = 2,	  /* Port partner is applying Rd */
	TYPEC_CC_VOLT_RP_DEF = 5, /* Port partner is applying Rp (0.5A) */
	TYPEC_CC_VOLT_RP_1_5 = 6, /* Port partner is applying Rp (1.5A) */
	TYPEC_CC_VOLT_RP_3_0 = 7, /* Port partner is applying Rp (3.0A) */
};

/* Resistor types we apply on our side of the CC lines */
enum tcpc_cc_pull {
	TYPEC_CC_RA = 0,
	TYPEC_CC_RP = 1,
	TYPEC_CC_RD = 2,
	TYPEC_CC_OPEN = 3,
	TYPEC_CC_RA_RD = 4, /* Powered cable with Sink */
};

/* Pull-up values we apply as a SRC to advertise different current limits */
enum tcpc_rp_value {
	TYPEC_RP_USB = 0,
	TYPEC_RP_1A5 = 1,
	TYPEC_RP_3A0 = 2,
	TYPEC_RP_RESERVED = 3,
};

enum tcpm_transmit_type {
	TCPC_TX_SOP = 0,
	TCPC_TX_SOP_PRIME = 1,
	TCPC_TX_SOP_PRIME_PRIME = 2,
	TCPC_TX_SOP_DEBUG_PRIME = 3,
	TCPC_TX_SOP_DEBUG_PRIME_PRIME = 4,
	TCPC_TX_HARD_RESET = 5,
	TCPC_TX_CABLE_RESET = 6,
	TCPC_TX_BIST_MODE_2 = 7
};

enum tcpc_transmit_complete {
	TCPC_TX_UNSET = -1,
	TCPC_TX_COMPLETE_SUCCESS =   0,
	TCPC_TX_COMPLETE_DISCARDED = 1,
	TCPC_TX_COMPLETE_FAILED =    2,
};

/**
 * Returns whether the sink has detected a Rp resistor on the other side.
 */
static inline int cc_is_rp(int cc)
{
	return (cc == TYPEC_CC_VOLT_RP_DEF) || (cc == TYPEC_CC_VOLT_RP_1_5) ||
	       (cc == TYPEC_CC_VOLT_RP_3_0);
}

/**
 * Returns true if both CC lines are completely open.
 */
static inline int cc_is_open(int cc1, int cc2)
{
	return cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN;
}

/**
 * Returns true if we detect the port partner is a snk debug accessory.
 */
static inline int cc_is_snk_dbg_acc(int cc1, int cc2)
{
	return cc1 == TYPEC_CC_VOLT_RD && cc2 == TYPEC_CC_VOLT_RD;
}

/**
 * Returns true if the port partner is an audio accessory.
 */
static inline int cc_is_audio_acc(int cc1, int cc2)
{
	return cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_RA;
}

/**
 * Returns true if the port partner is presenting at least one Rd
 */
static inline int cc_is_at_least_one_rd(int cc1, int cc2)
{
	return cc1 == TYPEC_CC_VOLT_RD || cc2 == TYPEC_CC_VOLT_RD;
}

/**
 * Returns true if the port partner is presenting Rd on only one CC line.
 */
static inline int cc_is_only_one_rd(int cc1, int cc2)
{
	return cc_is_at_least_one_rd(cc1, cc2) && cc1 != cc2;
}

struct tcpm_drv {
	/**
	 * Initialize TCPM driver and wait for TCPC readiness.
	 *
	 * @param port Type-C port number
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*init)(int port);

	/**
	 * Release the TCPM hardware and disconnect the driver.
	 * Only .init() can be called after .release().
	 *
	 * @param port Type-C port number
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*release)(int port);

	/**
	 * Read the CC line status.
	 *
	 * @param port Type-C port number
	 * @param cc1 pointer to CC status for CC1
	 * @param cc2 pointer to CC status for CC2
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_cc)(int port, int *cc1, int *cc2);

	/**
	 * Read VBUS
	 *
	 * @param port Type-C port number
	 *
	 * @return 0 => VBUS not detected, 1 => VBUS detected
	 */
	int (*get_vbus_level)(int port);

	/**
	 * Set the value of the CC pull-up used when we are a source.
	 *
	 * @param port Type-C port number
	 * @param rp One of enum tcpc_rp_value
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*select_rp_value)(int port, int rp);

	/**
	 * Set the CC pull resistor. This sets our role as either source or sink.
	 *
	 * @param port Type-C port number
	 * @param pull One of enum tcpc_cc_pull
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_cc)(int port, int pull);

	/**
	 * Set polarity
	 *
	 * @param port Type-C port number
	 * @param polarity 0=> transmit on CC1, 1=> transmit on CC2
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_polarity)(int port, int polarity);

	/**
	 * Set Vconn.
	 *
	 * @param port Type-C port number
	 * @param polarity Polarity of the CC line to read
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_vconn)(int port, int enable);

	/**
	 * Set PD message header to use for goodCRC
	 *
	 * @param port Type-C port number
	 * @param power_role Power role to use in header
	 * @param data_role Data role to use in header
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_msg_header)(int port, int power_role, int data_role);

	/**
	 * Set RX enable flag
	 *
	 * @param port Type-C port number
	 * @enable true for enable, false for disable
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_rx_enable)(int port, int enable);

	/**
	 * Read received PD message from the TCPC
	 *
	 * @param port Type-C port number
	 * @param payload Pointer to location to copy payload of message
	 * @param header of message
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_message_raw)(int port, uint32_t *payload, int *head);

	/**
	 * Transmit PD message
	 *
	 * @param port Type-C port number
	 * @param type Transmit type
	 * @param header Packet header
	 * @param cnt Number of bytes in payload
	 * @param data Payload
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*transmit)(int port, enum tcpm_transmit_type type, uint16_t header,
					const uint32_t *data);

	/**
	 * TCPC is asserting alert
	 *
	 * @param port Type-C port number
	 */
	void (*tcpc_alert)(int port);

	/**
	 * Discharge PD VBUS on src/sink disconnect & power role swap
	 *
	 * @param port Type-C port number
	 * @param enable Discharge enable or disable
	 */
	void (*tcpc_discharge_vbus)(int port, int enable);

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	/**
	 * Enable TCPC auto DRP toggling.
	 *
	 * @param port Type-C port number
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*drp_toggle)(int port);
#endif

	/**
	 * Get firmware version.
	 *
	 * @param port Type-C port number
	 * @param renew Force renewal
	 * @param info Pointer to pointer to PD chip info
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_chip_info)(int port, int renew,
			struct ec_response_pd_chip_info_v1 **info);

#ifdef CONFIG_USBC_PPC
	/**
	 * Send SinkVBUS or DisableSinkVBUS command
	 *
	 * @param port Type-C port number
	 * @enable true for enable, false for disable
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_snk_ctrl)(int port, int enable);

	/**
	 * Send SourceVBUS or DisableSourceVBUS command
	 *
	 * @param port Type-C port number
	 * @enable true for enable, false for disable
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_src_ctrl)(int port, int enable);
#endif

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/**
	 * Instructs the TCPC to enter into low power mode.
	 *
	 * NOTE: Do no use tcpc_(read|write) style helper methods in this
	 * function. You must use i2c_(read|write) directly.
	 *
	 * @param port Type-C port number
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*enter_low_power_mode)(int port);
#endif
};

/*
 * Macros for tcpc_config_t flags field.
 *
 * Bit 0 --> Polarity for TCPC alert. Set to 1 if alert is active high.
 * Bit 1 --> Set to 1 if TCPC alert line is open-drain instead of push-pull.
 * Bit 2 --> Polarity for TCPC reset. Set to 1 if reset line is active high.
 */
#define TCPC_FLAGS_ALERT_ACTIVE_HIGH	BIT(0)
#define TCPC_FLAGS_ALERT_OD		BIT(1)
#define TCPC_FLAGS_RESET_ACTIVE_HIGH	BIT(2)

struct tcpc_config_t {
	enum ec_bus_type bus_type;	/* enum ec_bus_type */
	union {
		struct i2c_info_t i2c_info;
	};
	const struct tcpm_drv *drv;
	/* See TCPC_FLAGS_* above */
	uint32_t flags;
#ifdef CONFIG_INTEL_VIRTUAL_MUX
	/*
	 * 0-3: Corresponding USB2 port number (1 ~ 15)
	 * 4-7: Corresponding USB3 port number (1 ~ 15)
	 */
	uint8_t usb23;
#endif
};

#ifndef CONFIG_USB_PD_TCPC_RUNTIME_CONFIG
extern const struct tcpc_config_t tcpc_config[];
#else
extern struct tcpc_config_t tcpc_config[];
#endif

/**
 * Returns the PD_STATUS_TCPC_ALERT_* mask corresponding to the TCPC ports
 * that are currently asserting ALERT.
 *
 * @return     PD_STATUS_TCPC_ALERT_* mask.
 */
uint16_t tcpc_get_alert_status(void);

/**
 * Optional, set the TCPC power mode.
 *
 * @param port Type-C port number
 * @param mode 0: off/sleep, 1: on/awake
 */
void board_set_tcpc_power_mode(int port, int mode) __attribute__((weak));

/**
 * Initialize TCPC.
 *
 * @param port Type-C port number
 */
void tcpc_init(int port);

/**
 * TCPC is asserting alert
 *
 * @param port Type-C port number
 */
void tcpc_alert_clear(int port);

/**
 * Run TCPC task once. This checks for incoming messages, processes
 * any outgoing messages, and reads CC lines.
 *
 * @param port Type-C port number
 * @param evt Event type that woke up this task
 */
int tcpc_run(int port, int evt);

/**
 * Initialize board specific TCPC functions post TCPC initialization.
 *
 * @param port Type-C port number
 *
 * @return EC_SUCCESS or error
 */
int board_tcpc_post_init(int port) __attribute__((weak));

#endif /* __CROS_EC_USB_PD_TCPM_H */
