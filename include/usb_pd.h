/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery module */

#ifndef __USB_PD_H
#define __USB_PD_H

#include "common.h"

enum pd_errors {
	PD_ERR_INVAL = -1,      /* Invalid packet */
	PD_ERR_HARD_RESET = -2, /* Got a Hard-Reset packet */
	PD_ERR_CRC = -3,        /* CRC mismatch */
	PD_ERR_ID = -4,         /* Invalid ID number */
};

/* incoming packet event (for the USB PD task) */
#define PD_EVENT_RX (1<<2)

/* --- PD data message helpers --- */

/* PDO : Power Data Object */
/*
 * 1. The vSafe5V Fixed Supply Object shall always be the first object.
 * 2. The remaining Fixed Supply Objects,
 *    if present, shall be sent in voltage order; lowest to highest.
 * 3. The Battery Supply Objects,
 *    if present shall be sent in Minimum Voltage order; lowest to highest.
 * 4. The Variable Supply (non battery) Objects,
 *    if present, shall be sent in Minimum Voltage order; lowest to highest.
 */
#define PDO_TYPE_FIXED    (0 << 30)
#define PDO_TYPE_BATTERY  (1 << 30)
#define PDO_TYPE_VARIABLE (2 << 30)
#define PDO_TYPE_MASK     (3 << 30)

#define PDO_FIXED_DUAL_ROLE (1 << 29) /* Dual role device */
#define PDO_FIXED_SUSPEND   (1 << 28) /* USB Suspend supported */
#define PDO_FIXED_EXTERNAL  (1 << 27) /* Externally powered */
#define PDO_FIXED_COMM_CAP  (1 << 26) /* USB Communications Capable */
#define PDO_FIXED_PEAK_CURR () /* [21..20] Peak current */
#define PDO_FIXED_VOLT(mv)  (((mv)/50) << 10) /* Voltage in 50mV units */
#define PDO_FIXED_CURR(ma)  (((ma)/10) << 0)  /* Max current in 10mA units */

#define PDO_FIXED(mv, ma, flags) (PDO_FIXED_VOLT(mv) |\
				  PDO_FIXED_CURR(ma) | (flags))

#define PDO_VAR_MAX_VOLT(mv) ((((mv) / 50) & 0x3FF) << 20)
#define PDO_VAR_MIN_VOLT(mv) ((((mv) / 50) & 0x3FF) << 10)
#define PDO_VAR_OP_CURR(ma)  ((((ma) / 10) & 0x3FF) << 0)

#define PDO_VAR(min_mv, max_mv, op_ma) \
				(PDO_VAR_MIN_VOLT(min_mv) | \
				 PDO_VAR_MAX_VOLT(max_mv) | \
				 PDO_VAR_OP_CURR(op_ma))

#define PDO_BATT_MAX_VOLT(mv) ((((mv) / 50) & 0x3FF) << 20)
#define PDO_BATT_MIN_VOLT(mv) ((((mv) / 50) & 0x3FF) << 10)
#define PDO_BATT_OP_POWER(mw) ((((mw) / 10) & 0x3FF) << 0)

#define PDO_BATT(min_mv, max_mv, op_mw) \
				(PDO_BATT_MIN_VOLT(min_mv) | \
				 PDO_BATT_MAX_VOLT(max_mv) | \
				 PDO_BATT_OP_POWER(op_mw))

/* RDO : Request Data Object */
#define RDO_OBJ_POS(n)             (((n) & 0x7) << 28)
#define RDO_GIVE_BACK              (1 << 27)
#define RDO_CAP_MISMATCH           (1 << 26)
#define RDO_COMM_CAP               (1 << 25)
#define RDO_NO_SUSPEND             (1 << 24)
#define RDO_FIXED_VAR_OP_CURR(ma)  ((((ma) / 10) & 0x3FF) << 10)
#define RDO_FIXED_VAR_MAX_CURR(ma) ((((ma) / 10) & 0x3FF) << 0)

#define RDO_BATT_OP_POWER(mw)      ((((mw) / 250) & 0x3FF) << 10)
#define RDO_BATT_MAX_POWER(mw)     ((((mw) / 250) & 0x3FF) << 10)

#define RDO_FIXED(n, op_ma, max_ma, flags) \
				(RDO_OBJ_POS(n) | (flags) | \
				RDO_FIXED_VAR_OP_CURR(op_ma) | \
				RDO_FIXED_VAR_MAX_CURR(max_ma))


#define RDO_BATT(n, op_mw, max_mw, flags) \
				(RDO_OBJ_POS(n) | (flags) | \
				RDO_BATT_OP_POWER(op_mw) | \
				RDO_BATT_MAX_POWER(max_mw))

/* BDO : BIST Data Object */
#define BDO_MODE_RECV       (0 << 28)
#define BDO_MODE_TRANSMIT   (1 << 28)
#define BDO_MODE_COUNTERS   (2 << 28)
#define BDO_MODE_CARRIER0   (3 << 28)
#define BDO_MODE_CARRIER1   (4 << 28)
#define BDO_MODE_CARRIER2   (5 << 28)
#define BDO_MODE_CARRIER3   (6 << 28)
#define BDO_MODE_EYE        (7 << 28)

#define BDO(mode, cnt)      ((mode) | ((cnt) & 0xFFFF))

/* VDO : Vendor Defined Message Object */
#define VDO(vid, custom) (((vid) << 16) | ((custom) & 0xFFFF))

/* --- Policy layer functions --- */

/**
 * Decide which voltage to use from the source capabilities.
 *
 * @param cnt  the number of Power Data Objects.
 * @param src_caps Power Data Objects representing the source capabilities.
 * @param rdo  requested Request Data Object.
 * @return EC_SUCCESS if the RDO is filled with valid data, <0 else.
 */
int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo);

/**
 * Put a cap on the max voltage requested as a sink.
 * @param mv maximum voltage in millivolts.
 */
void pd_set_max_voltage(unsigned mv);

/**
 * Request a new operating voltage.
 *
 * @param rdo  Request Data Object with the selected operating point.
 * @return EC_SUCCESS if we can get the requested voltage/OP, <0 else.
 */
int pd_request_voltage(uint32_t rdo);

/**
 * Go back to the default/safe state of the power supply
 */
void pd_power_supply_reset(void);

/**
 * Enable the power supply output after the ready delay.
 *
 * @return EC_SUCCESS if the power supply is ready, <0 else.
 */
int pd_set_power_supply_ready(void);

/**
 * Ask the specified voltage from the PD source.
 *
 * It triggers a new negotiation sequence with the source.
 * @param mv request voltage in millivolts.
 */
void pd_request_source_voltage(int mv);

/*
 * Verify board specific health status : current, voltages...
 *
 */
void pd_board_checks(void);

/* Power Data Objects for the source and the sink */
extern const uint32_t pd_src_pdo[];
extern const int pd_src_pdo_cnt;
extern const uint32_t pd_snk_pdo[];
extern const int pd_snk_pdo_cnt;

/* --- Physical layer functions : chip specific --- */

/* Packet preparation/retrieval */

/**
 * Prepare packet reading state machine.
 *
 * @return opaque context for other reading functions.
 */
void *pd_init_dequeue(void);

/**
 * Prepare packet reading state machine.
 *
 * @param ctxt opaque context.
 * @param off  current position in the packet buffer.
 * @param len  minimum size to read in bits.
 * @param val  the read bits.
 * @return new position in the packet buffer.
 */
int pd_dequeue_bits(void *ctxt, int off, int len, uint32_t *val);

/**
 * Advance until the end of the preamble.
 *
 * @param ctxt opaque context.
 * @return new position in the packet buffer.
 */
int pd_find_preamble(void *ctxt);

/**
 * Write the preamble in the TX buffer.
 *
 * @param ctxt opaque context.
 * @return new position in the packet buffer.
 */
int pd_write_preamble(void *ctxt);

/**
 * Write one 10-period symbol in the TX packet.
 * corresponding to a quartet with 4b5b encoding
 * and Biphase Mark Coding.
 *
 * @param ctxt    opaque context.
 * @param bit_off current position in the packet buffer.
 * @param val10    the 10-bit integer.
 * @return new position in the packet buffer.
 */
int pd_write_sym(void *ctxt, int bit_off, uint32_t val10);


/**
 * Ensure that we have an edge after EOP and we end up at level 0,
 * also fill the last byte.
 *
 * @param ctxt    opaque context.
 * @param bit_off current position in the packet buffer.
 * @return new position in the packet buffer.
 */
int pd_write_last_edge(void *ctxt, int bit_off);

/**
 * Dump the current PD packet on the console for debug.
 *
 * @param ctxt opaque context.
 * @param msg  context string.
 */
void pd_dump_packet(void *ctxt, const char *msg);

/**
 * Change the TX data clock frequency.
 *
 * @param freq frequency in hertz.
 */
void pd_set_clock(int freq);

/* TX/RX callbacks */

/**
 * Start sending over the wire the prepared packet.
 *
 * @param ctxt    opaque context.
 * @param polarity plug polarity (0=CC1, 1=CC2).
 * @param bit_len size of the packet in bits.
 */
void pd_start_tx(void *ctxt, int polarity, int bit_len);
/**
 * Call when we are done sending a packet.
 *
 * @param polarity plug polarity (0=CC1, 1=CC2).
 */
void pd_tx_done(int polarity);

/**
 * Check whether the PD reception is started.
 *
 * @return true if the reception is on-going.
 */
int pd_rx_started(void);

/* Callback when the hardware has detected an incoming packet */
void pd_rx_event(void);
/* Start sampling the CC line for reception */
void pd_rx_start(void);
/* Call when we are done reading a packet */
void pd_rx_complete(void);

/* restart listening to the CC wire */
void pd_rx_enable_monitoring(void);
/* stop listening to the CC wire during transmissions */
void pd_rx_disable_monitoring(void);

/**
 * Initialize the hardware used for PD RX/TX.
 *
 * @return opaque context for other functions.
 */
void *pd_hw_init(void);

#endif  /* __USB_PD_H */
