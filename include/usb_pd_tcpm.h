/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_H
#define __CROS_EC_USB_PD_TCPM_H

#include <stdbool.h>
#include "ec_commands.h"
#include "i2c.h"

/* Default retry count for transmitting */
#define PD_RETRY_COUNT 3

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT  (100*MSEC)

enum usbpd_cc_pin {
	USBPD_CC_PIN_1,
	USBPD_CC_PIN_2,
};

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

enum tcpc_cc_polarity {
	/*
	 * _CCx: is used to indicate the polarity while not connected to
	 * a Debug Accessory.  Only one CC line will assert a resistor and
	 * the other will be open.
	 */
	POLARITY_CC1 = 0,
	POLARITY_CC2 = 1,

	/*
	 * CCx_DTS is used to indicate the polarity while connected to a
	 * SRC Debug Accessory.  Assert resistors on both lines.
	 */
	POLARITY_CC1_DTS = 2,
	POLARITY_CC2_DTS = 3,

	/*
	 * The current TCPC code relies on these specific POLARITY values.
	 * Adding in a check to verify if the list grows for any reason
	 * that this will give a hint that other places need to be
	 * adjusted.
	 */
	POLARITY_COUNT
};

/**
 * Returns whether the polarity without the DTS extension
 */
static inline enum tcpc_cc_polarity polarity_rm_dts(
	enum tcpc_cc_polarity polarity)
{
	BUILD_ASSERT(POLARITY_COUNT == 4);
	return polarity & BIT(0);
}

enum tcpm_transmit_type {
	TCPC_TX_SOP = 0,
	TCPC_TX_SOP_PRIME = 1,
	TCPC_TX_SOP_PRIME_PRIME = 2,
	TCPC_TX_SOP_DEBUG_PRIME = 3,
	TCPC_TX_SOP_DEBUG_PRIME_PRIME = 4,
	TCPC_TX_HARD_RESET = 5,
	TCPC_TX_CABLE_RESET = 6,
	TCPC_TX_BIST_MODE_2 = 7,
	TCPC_TX_INVALID = 0xf,
};

/* Number of valid Transmit Types */
#define NUM_SOP_STAR_TYPES (TCPC_TX_SOP_DEBUG_PRIME_PRIME + 1)

enum tcpc_transmit_complete {
	TCPC_TX_UNSET = -1,
	TCPC_TX_COMPLETE_SUCCESS =   0,
	TCPC_TX_COMPLETE_DISCARDED = 1,
	TCPC_TX_COMPLETE_FAILED =    2,
};

/* USB-C PD Vbus levels */
enum vbus_level {
	VBUS_SAFE0V,
	VBUS_PRESENT,
};

/**
 * Returns whether the sink has detected a Rp resistor on the other side.
 */
static inline int cc_is_rp(enum tcpc_cc_voltage_status cc)
{
	return (cc == TYPEC_CC_VOLT_RP_DEF) || (cc == TYPEC_CC_VOLT_RP_1_5) ||
	       (cc == TYPEC_CC_VOLT_RP_3_0);
}

/**
 * Returns true if both CC lines are completely open.
 */
static inline int cc_is_open(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	return cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN;
}

/**
 * Returns true if we detect the port partner is a snk debug accessory.
 */
static inline int cc_is_snk_dbg_acc(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	return cc1 == TYPEC_CC_VOLT_RD && cc2 == TYPEC_CC_VOLT_RD;
}

/**
 * Returns true if we detect the port partner is a src debug accessory.
 */
static inline int cc_is_src_dbg_acc(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	return cc_is_rp(cc1) && cc_is_rp(cc2);
}

/**
 * Returns true if the port partner is an audio accessory.
 */
static inline int cc_is_audio_acc(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	return cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_RA;
}

/**
 * Returns true if the port partner is presenting at least one Rd
 */
static inline int cc_is_at_least_one_rd(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	return cc1 == TYPEC_CC_VOLT_RD || cc2 == TYPEC_CC_VOLT_RD;
}

/**
 * Returns true if the port partner is presenting Rd on only one CC line.
 */
static inline int cc_is_only_one_rd(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
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
	int (*get_cc)(int port, enum tcpc_cc_voltage_status *cc1,
		enum tcpc_cc_voltage_status *cc2);

	/**
	 * Check VBUS level
	 *
	 * @param port Type-C port number
	 * @param level safe level voltage to check against
	 *
	 * @return False => VBUS not at level, True => VBUS at level
	 */
	bool (*check_vbus_level)(int port, enum vbus_level level);

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
	 * @param polarity port polarity
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_polarity)(int port, enum tcpc_cc_polarity polarity);

	/**
	 * Set Vconn.
	 *
	 * @param port Type-C port number
	 * @param enable Enable/Disable Vconn
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

	/**
	 * Auto Discharge Disconnect
	 *
	 * @param port Type-C port number
	 * @param enable Auto Discharge enable or disable
	 */
	void (*tcpc_enable_auto_discharge_disconnect)(int port,
						      int enable);

	/**
	 * Manual control of TCPC DebugAccessory enable
	 *
	 * @param port Type-C port number
	 * @param enable Debug Accessory enable or disable
	 */
	int (*debug_accessory)(int port, bool enable);

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
	 * @param live Fetch live chip info or hard-coded + cached info
	 * @param info Pointer to PD chip info; NULL to cache the info only
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_chip_info)(int port, int live,
			struct ec_response_pd_chip_info_v1 *info);

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

#ifdef CONFIG_USB_PD_FRS_TCPC
	/**
	 * Enable/Disable TCPC FRS detection
	 *
	 * @param port Type-C port number
	 * @param enable FRS enable (true) disable (false)
	 *
	 * @return EC_SUCCESS or error
	 */
	 int (*set_frs_enable)(int port, int enable);
#endif

	/**
	 * Handle TCPCI Faults
	 *
	 * @param port Type-C port number
	 * @param fault TCPCI fault status value
	 *
	 * @return EC_SUCCESS or error
	 */
	 int (*handle_fault)(int port, int fault);

#ifdef CONFIG_CMD_TCPC_DUMP
	/**
	 * Dump TCPC registers
	 *
	 * @param port Type-C port number
	 */
	 void (*dump_registers)(int port);
#endif /* defined(CONFIG_CMD_TCPC_DUMP) */
};

/*
 * Macros for tcpc_config_t flags field.
 *
 * Bit 0 --> Polarity for TCPC alert. Set to 1 if alert is active high.
 * Bit 1 --> Set to 1 if TCPC alert line is open-drain instead of push-pull.
 * Bit 2 --> Polarity for TCPC reset. Set to 1 if reset line is active high.
 * Bit 3 --> Set to 1 if TCPC is using TCPCI Revision 2.0
 * Bit 4 --> Set to 1 if TCPC is using TCPCI Revision 2.0 but does not support
 *           the vSafe0V bit in the EXTENDED_STATUS_REGISTER
 */
#define TCPC_FLAGS_ALERT_ACTIVE_HIGH	BIT(0)
#define TCPC_FLAGS_ALERT_OD		BIT(1)
#define TCPC_FLAGS_RESET_ACTIVE_HIGH	BIT(2)
#define TCPC_FLAGS_TCPCI_REV2_0		BIT(3)
#define TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V	BIT(4)

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

/**
 * Turn on/off VCONN power switch in board specific code.
 *
 * @param port Type-C port number
 * @param cc_pin 0:CC pin 0, 1: CC pin 1
 * @param enabled 1: Enable VCONN, 0: Disable VCONN
 *
 */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled);

/**
 * Get the VBUS voltage from TCPC
 *
 * @param port Type-C port number
 *
 * @return VBUS voltage in mV.
 */
int tcpc_get_vbus_voltage(int port);

#ifdef CONFIG_CMD_TCPC_DUMP
struct tcpc_reg_dump_map {
	uint8_t		addr;
	uint8_t		size;
	const char	*name;
};

/**
 * Dump the standard TCPC registers.
 *
 * @param port Type-C port number
 *
 */
void tcpc_dump_std_registers(int port);

/**
 * Dump chip specific TCPC registers.
 *
 * @param port Type-C port number
 * @param pointer to table of registers and names
 * @param count of registers to dump
 *
 */
void tcpc_dump_registers(int port, const struct tcpc_reg_dump_map *reg,
			  int count);
#endif
#endif /* __CROS_EC_USB_PD_TCPM_H */
