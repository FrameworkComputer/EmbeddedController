/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_USB_PD_TCPM_H
#define __CROS_EC_USB_PD_TCPM_H

#include "common.h"
#include "compiler.h"
#include "ec_commands.h"
#include "i2c.h"

#include <stdbool.h>

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT (100 * MSEC)

enum usbpd_cc_pin {
	USBPD_CC_PIN_1,
	USBPD_CC_PIN_2,
};

/* Detected resistor values of port partner */
enum tcpc_cc_voltage_status {
	TYPEC_CC_VOLT_OPEN = 0,
	TYPEC_CC_VOLT_RA = 1, /* Port partner is applying Ra */
	TYPEC_CC_VOLT_RD = 2, /* Port partner is applying Rd */
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
FORWARD_DECLARE_ENUM(tcpc_rp_value){
	TYPEC_RP_USB = 0,
	TYPEC_RP_1A5 = 1,
	TYPEC_RP_3A0 = 2,
	TYPEC_RP_RESERVED = 3,
};

/* DRP (dual-role-power) setting */
enum tcpc_drp {
	TYPEC_NO_DRP = 0,
	TYPEC_DRP = 1,
};

/**
 * Returns whether the polarity without the DTS extension
 */
static inline enum tcpc_cc_polarity
polarity_rm_dts(enum tcpc_cc_polarity polarity)
{
	BUILD_ASSERT(POLARITY_COUNT == 4);
	return (enum tcpc_cc_polarity)(polarity & BIT(0));
}

/*
 * Types of PD data that can be sent or received. The values match the TCPCI bit
 * field values TRANSMIT[Transmit SOP* Message] (TCPCI r2.0 v1.2, table 4-38)
 * and RX_BUF_FRAME_TYPE[Received SOP* message] (table 4-37). Note that Hard
 * Reset, Cable Reset, and BIST Carrier Mode 2 are not really messages.
 */
enum tcpci_msg_type {
	TCPCI_MSG_SOP = 0,
	TCPCI_MSG_SOP_PRIME = 1,
	TCPCI_MSG_SOP_PRIME_PRIME = 2,
	TCPCI_MSG_SOP_DEBUG_PRIME = 3,
	TCPCI_MSG_SOP_DEBUG_PRIME_PRIME = 4,
	/* Only a valid register setting for TRANSMIT */
	TCPCI_MSG_TX_HARD_RESET = 5,
	TCPCI_MSG_CABLE_RESET = 6,
	/* Only a valid register setting for TRANSMIT */
	TCPCI_MSG_TX_BIST_MODE_2 = 7,
	TCPCI_MSG_INVALID = 0xf,
};

/* Number of valid Transmit Types */
#define NUM_SOP_STAR_TYPES (TCPCI_MSG_SOP_DEBUG_PRIME_PRIME + 1)

enum tcpc_transmit_complete {
	TCPC_TX_UNSET = -1,
	TCPC_TX_WAIT = 0,
	TCPC_TX_COMPLETE_SUCCESS = 1,
	TCPC_TX_COMPLETE_DISCARDED = 2,
	TCPC_TX_COMPLETE_FAILED = 3,
};

/*
 * USB-C PD Vbus levels
 *
 * Return true on Vbus check if Vbus is...
 */
enum vbus_level {
	VBUS_SAFE0V, /* less than vSafe0V max */
	VBUS_PRESENT, /* at least vSafe5V min */
	VBUS_REMOVED, /* less than vSinkDisconnect max */
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
 * Returns true if we detect a powered cable without sink attached.
 * This is a pair of Ra and Open.
 */
static inline int cc_is_pwred_cbl_without_snk(enum tcpc_cc_voltage_status cc1,
					      enum tcpc_cc_voltage_status cc2)
{
	return (cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_OPEN) ||
	       (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_RA);
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
	 * Get VBUS voltage
	 *
	 * @param port Type-C port number
	 * @param vbus read VBUS voltage in mV
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_vbus_voltage)(int port, int *vbus);

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
	 * Set the CC pull resistor. This sets our role as either source or
	 * sink.
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

#ifdef CONFIG_USB_PD_DECODE_SOP
	/**
	 * Control receive of SOP' and SOP'' messages. This is provided
	 * separately from set_vconn so that we can preemptively disable
	 * receipt of SOP' messages during a VCONN swap, or disable during spans
	 * when port partners may erroneously be sending cable messages.
	 *
	 * @param port Type-C port number
	 * @param enable Enable SOP' and SOP'' messages
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*sop_prime_enable)(int port, bool enable);
#endif

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
	int (*transmit)(int port, enum tcpci_msg_type type, uint16_t header,
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
	void (*tcpc_enable_auto_discharge_disconnect)(int port, int enable);

	/**
	 * Manual control of TCPC DebugAccessory enable
	 *
	 * @param port Type-C port number
	 * @param enable Debug Accessory enable or disable
	 */
	int (*debug_accessory)(int port, bool enable);

	/**
	 * Break debug connection, if TCPC requires specific commands to be run
	 * in order to correctly exit a debug connection.
	 *
	 * @param port Type-C port number
	 */
	int (*debug_detach)(int port);

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

	/**
	 * Request current sinking state of the TCPC
	 * NOTE: this is most useful for PPCs that can not tell on their own
	 *
	 * @param port Type-C port number
	 *
	 * @return true if sinking else false
	 */
	bool (*get_snk_ctrl)(int port);

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
	 * Request current sourcing state of the TCPC
	 * NOTE: this is most useful for PPCs that can not tell on their own
	 *
	 * @param port Type-C port number
	 *
	 * @return true if sourcing else false
	 */
	bool (*get_src_ctrl)(int port);

	/**
	 * Send SourceVBUS or DisableSourceVBUS command
	 *
	 * @param port Type-C port number
	 * @enable true for enable, false for disable
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_src_ctrl)(int port, int enable);

#ifdef CONFIG_USB_PD_TCPM_SBU
	/*
	 * Enable SBU lines.
	 *
	 * Some PD chips have integrated port protection for SBU lines and the
	 * switches to enable the SBU lines coming out of the PD chips are
	 * controlled by vendor specific registers. Hence, this function has to
	 * be written in vendor specific driver code and the board specific
	 * tcpc_config[] has to initialize the function with vendor specific
	 * function at board level.
	 *
	 * @param port Type-C port number
	 * @enable true for enable, false for disable
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_sbu)(int port, bool enable);
#endif /* CONFIG_USB_PD_TCPM_SBU */

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

	/**
	 * Starts I2C wake sequence for TCPC
	 *
	 * NOTE: Do no use tcpc_(read|write) style helper methods in this
	 * function. You must use i2c_(read|write) directly.
	 *
	 * @param port Type-C port number
	 */
	void (*wake_low_power_mode)(int port);
#endif

#ifdef CONFIG_USB_PD_FRS
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

	/**
	 * Re-initialize registers during hard reset
	 *
	 * NOTE: If the function alters the alert mask and power status mask,
	 * this indicates the chip does not require a full TCPCI re-init after
	 * a hard reset.
	 *
	 * @param port Type-C port number
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*hard_reset_reinit)(int port);

	/**
	 * Controls BIST Test Mode (or analogous functionality) in the TCPC and
	 * associated behavior changes. Disables message Rx alerts while the
	 * port is in Test Mode.
	 *
	 * @param port   USB-C port number
	 * @param enable true to enter BIST Test Mode; false to exit
	 * @return EC_SUCCESS or error code
	 */
	enum ec_error_list (*set_bist_test_mode)(int port, bool enable);

	/**
	 * Get control of BIST Test Mode (or analogous functionality) in the
	 * TCPC.
	 *
	 * @param port   USB-C port number
	 * @param enable true for BIST Test Mode enabled; false for error
	 *               occurred or BIST Test Mode disabled.
	 * @return EC_SUCCESS or error code
	 */
	enum ec_error_list (*get_bist_test_mode)(int port, bool *enable);
#ifdef CONFIG_CMD_TCPC_DUMP
	/**
	 * Dump TCPC registers
	 *
	 * @param port Type-C port number
	 */
	void (*dump_registers)(int port);
#endif /* defined(CONFIG_CMD_TCPC_DUMP) */

	int (*reset_bist_type_2)(int port);

#ifdef CONFIG_MFD
	/**
	 * Optional - lock the TCPC port for exclusive I2C access.  Only
	 * supported on Zephyr projects that use multi-function devices
	 * for the TCPC.
	 *
	 * @param part USB-C port number
	 * @param get_lock Non zero to acquire lock, zero to release lock
	 */
	void (*lock)(int port, int get_lock);
#endif
};

#ifdef CONFIG_ZEPHYR

#include "dt-bindings/usb_pd_tcpm.h"

#else /* !CONFIG_ZEPHYR */

/*
 * Macros for tcpc_config_t flags field.
 *
 * Bit 0 --> Polarity for TCPC alert. Set to 1 if alert is active high.
 * Bit 1 --> Set to 1 if TCPC alert line is open-drain instead of push-pull.
 * Bit 2 --> Polarity for TCPC reset. Set to 1 if reset line is active high.
 * Bit 3 --> Set to 1 if TCPC is using TCPCI Revision 2.0
 * Bit 4 --> Set to 1 if TCPC is using TCPCI Revision 2.0 but does not support
 *           the vSafe0V bit in the EXTENDED_STATUS_REGISTER
 * Bit 5 --> Set to 1 to prevent TCPC setting debug accessory control
 * Bit 6 --> TCPC controls VCONN (even when CONFIG_USB_PD_TCPC_VCONN is off)
 * Bit 7 --> TCPC controls FRS (even when CONFIG_USB_PD_FRS_TCPC is off)
 * Bit 8 --> TCPC enable VBUS monitoring
 */
#define TCPC_FLAGS_ALERT_ACTIVE_HIGH BIT(0)
#define TCPC_FLAGS_ALERT_OD BIT(1)
#define TCPC_FLAGS_RESET_ACTIVE_HIGH BIT(2)
#define TCPC_FLAGS_TCPCI_REV2_0 BIT(3)
#define TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V BIT(4)
#define TCPC_FLAGS_NO_DEBUG_ACC_CONTROL BIT(5)
#define TCPC_FLAGS_CONTROL_VCONN BIT(6)
#define TCPC_FLAGS_CONTROL_FRS BIT(7)
#define TCPC_FLAGS_VBUS_MONITOR BIT(8)

#endif /* !CONFIG_ZEPHYR */

struct tcpc_config_t {
	enum ec_bus_type bus_type; /* enum ec_bus_type */
	union {
		struct i2c_info_t i2c_info;
	};
	const struct tcpm_drv *drv;
	/* See TCPC_FLAGS_* above */
	uint32_t flags;
#ifdef CONFIG_PLATFORM_EC_TCPC_INTERRUPT
	struct gpio_dt_spec irq_gpio;
	struct gpio_dt_spec rst_gpio;
#else
	enum gpio_signal alert_signal;
#endif
#ifdef CONFIG_MFD
	/*
	 * Some TCPCs are multi-function devices supporting a TCPC
	 * function and other functions (I/O expander, PPC, BC1.2, etc).
	 * When Zephyr upstream provides a parent multi-function driver,
	 * this field gets set to the corresponding device so the TCPC
	 * driver can lock access and prevent corruption.
	 */
	const struct device *mfd_parent;
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

#ifdef CONFIG_CMD_TCPC_DUMP
struct tcpc_reg_dump_map {
	uint8_t addr;
	uint8_t size;
	const char *name;
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
