/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32GX UCPD module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/tcpm.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "ucpd-stm32gx.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

#define USB_VID_STM32 0x0483

/*
 * USB PD message buffer length. Absent extended messages, the longest PD
 * message will be 7 objects (4 bytes each) plus a 2 byte header. TCPMv2
 * suports extended messages via chunking so the data buffer length is
 * set assumign that extended messages are chunked.
 */
#define UCPD_BUF_LEN 30

#define UCPD_IMR_RX_INT_MASK                                   \
	(STM32_UCPD_IMR_RXNEIE | STM32_UCPD_IMR_RXORDDETIE |   \
	 STM32_UCPD_IMR_RXHRSTDETIE | STM32_UCPD_IMR_RXOVRIE | \
	 STM32_UCPD_IMR_RXMSGENDIE)

#define UCPD_IMR_TX_INT_MASK                                      \
	(STM32_UCPD_IMR_TXISIE | STM32_UCPD_IMR_TXMSGDISCIE |     \
	 STM32_UCPD_IMR_TXMSGSENTIE | STM32_UCPD_IMR_TXMSGABTIE | \
	 STM32_UCPD_IMR_TXUNDIE)

#define UCPD_ICR_TX_INT_MASK                                       \
	(STM32_UCPD_ICR_TXMSGDISCCF | STM32_UCPD_ICR_TXMSGSENTCF | \
	 STM32_UCPD_ICR_TXMSGABTCF | STM32_UCPD_ICR_TXUNDCF)

#define UCPD_ANASUB_TO_RP(r) ((r - 1) & 0x3)
#define UCPD_RP_TO_ANASUB(r) ((r + 1) & 0x3)

struct msg_header_info {
	enum pd_power_role pr;
	enum pd_data_role dr;
};
static struct msg_header_info msg_header;

/* States for managing tx messages in ucpd task */
enum ucpd_state {
	STATE_IDLE,
	STATE_ACTIVE_TCPM,
	STATE_ACTIVE_CRC,
	STATE_HARD_RESET,
	STATE_WAIT_CRC_ACK,
};

/* Events for pd_interrupt_handler_task */
#define UCPD_EVT_GOOD_CRC_REQ BIT(0)
#define UCPD_EVT_TCPM_MSG_REQ BIT(1)
#define UCPD_EVT_HR_REQ BIT(2)
#define UCPD_EVT_TX_MSG_FAIL BIT(3)
#define UCPD_EVT_TX_MSG_DISC BIT(4)
#define UCPD_EVT_TX_MSG_SUCCESS BIT(5)
#define UCPD_EVT_HR_DONE BIT(6)
#define UCPD_EVT_HR_FAIL BIT(7)
#define UCPD_EVT_RX_GOOD_CRC BIT(8)
#define UCPD_EVT_RX_MSG BIT(9)

#define UCPD_T_RECEIVE_US (1 * MSEC)

#define UCPD_N_RETRY_COUNT_REV20 3
#define UCPD_N_RETRY_COUNT_REV30 2

/*
 * Tx messages are iniated either by TCPM/PRL layer or from ucpd when a GoodCRC
 * ack message needs to be sent.
 */
enum ucpd_tx_msg {
	TX_MSG_NONE = -1,
	TX_MSG_TCPM = 0,
	TX_MSG_GOOD_CRC = 1,
	TX_MSG_TOTAL = 2,
};

#define MSG_TCPM_MASK BIT(TX_MSG_TCPM)
#define MSG_GOOD_CRC_MASK BIT(TX_MSG_GOOD_CRC)

union buffer {
	uint16_t header;
	uint8_t msg[UCPD_BUF_LEN];
};

struct ucpd_tx_desc {
	enum tcpci_msg_type type;
	int msg_len;
	int msg_index;
	union buffer data;
};

/* Track VCONN on/off state */
static int ucpd_vconn_enable;

/* Tx message variables */
struct ucpd_tx_desc ucpd_tx_buffers[TX_MSG_TOTAL];
struct ucpd_tx_desc *ucpd_tx_active_buffer;
static int ucpd_tx_request;
static int ucpd_timeout_us;
static enum ucpd_state ucpd_tx_state;
static int msg_id_match;
static int tx_retry_count;
static int tx_retry_max;

static int ucpd_txorderset[] = {
	TX_ORDERSET_SOP,
	TX_ORDERSET_SOP_PRIME,
	TX_ORDERSET_SOP_PRIME_PRIME,
	TX_ORDERSET_SOP_PRIME_DEBUG,
	TX_ORDERSET_SOP_PRIME_PRIME_DEBUG,
	TX_ORDERSET_HARD_RESET,
	TX_ORDERSET_CABLE_RESET,
};

/* PD Rx variables */
static int ucpd_rx_byte_count;
static uint8_t ucpd_rx_buffer[UCPD_BUF_LEN];
static int ucpd_crc_id;
static bool ucpd_rx_sop_prime_enabled;
static int ucpd_rx_msg_active;
static bool ucpd_rx_bist_mode;

#ifdef CONFIG_STM32G4_UCPD_DEBUG
/* Defines and macros for ucpd state logging */
#define TX_STATE_LOG_LEN BIT(5)
#define TX_STATE_LOG_MASK (TX_STATE_LOG_LEN - 1)

struct ucpd_tx_state {
	uint32_t ts;
	int tx_request;
	int timeout_us;
	enum ucpd_state enter_state;
	enum ucpd_state exit_state;
	uint32_t evt;
};

struct ucpd_tx_state ucpd_tx_statelog[TX_STATE_LOG_LEN];
int ucpd_tx_state_log_idx;
int ucpd_tx_state_log_freeze;

static char ucpd_names[][12] = {
	"TX_IDLE", "ACT_TCPM", "ACT_CRC", "HARD_RST", "WAIT_CRC",
};
/* Defines and macros used for ucpd pd message logging */
#define MSG_LOG_LEN 64
#define MSG_BUF_LEN 10

struct msg_info {
	uint8_t dir;
	uint8_t comp;
	uint8_t crc;
	uint16_t header;
	uint32_t ts;
	uint8_t buf[MSG_BUF_LEN];
};
static int msg_log_cnt;
static int msg_log_idx;
static struct msg_info msg_log[MSG_LOG_LEN];

#define UCPD_CC_STRING_LEN 5

static char ccx[4][UCPD_CC_STRING_LEN] = {
	"Ra",
	"Rp",
	"Rd",
	"Open",
};
static char rp_string[][8] = {
	"Rp_usb",
	"Rp_1.5",
	"Rp_3.0",
	"Open",
};
static int ucpd_sr_cc_event;
static int ucpd_cc_set_save;
static int ucpd_cc_change_log;

static int ucpd_is_cc_pull_active(int port, enum usbpd_cc_pin cc_line);

static void ucpd_log_add_msg(uint16_t header, int dir)
{
	uint32_t ts = __hw_clock_source_read();
	int idx = msg_log_idx;
	uint8_t *buf = dir ? ucpd_rx_buffer : ucpd_tx_active_buffer->data.msg;

	/*
	 * Add a msg entry in the history log. The log is currently designed to
	 * be from reset until MSG_LOG_LEN messages have been added.
	 * ts -> lower 32 bits of 1 uSec running clock
	 * dir -> 0 = tx message, 1 = rx message
	 * comp -> ucpd transmit success
	 * crc -> GoodCrc received following tx message
	 */
	if (msg_log_cnt++ < MSG_LOG_LEN) {
		int msg_bytes =
			MIN((PD_HEADER_CNT(header) << 2) + 2, MSG_BUF_LEN);

		msg_log[idx].header = header;
		msg_log[idx].ts = ts;
		msg_log[idx].dir = dir;
		msg_log[idx].comp = 0;
		msg_log[idx].crc = 0;
		msg_log_idx++;
		memcpy(msg_log[idx].buf, buf, msg_bytes);
	}
}

static void ucpd_log_mark_tx_comp(void)
{
	/*
	 * This msg logging utility function is used to mark when a message was
	 * successfully transmitted when transmit interrupt occurs and the tx
	 * message sent status was set. Because the transmit message is added
	 * before it's sent by ucpd, the index has to back up one to mark the
	 * correct log entry.
	 */
	if (msg_log_cnt < MSG_LOG_LEN) {
		if (msg_log_idx > 0)
			msg_log[msg_log_idx - 1].comp = 1;
	}
}

static void ucpd_log_mark_crc(void)
{
	/*
	 * This msg logging utility function is used to mark when a GoodCRC
	 * message is received following a tx message. This status is displayed
	 * in column s2. Because this indication follows both transmit message
	 * and GoodCRC rx, the index must be back up 2 rows to mark the correct
	 * tx message entry.
	 */
	if (msg_log_cnt < MSG_LOG_LEN) {
		if (msg_log_idx >= 2)
			msg_log[msg_log_idx - 2].crc = 1;
	}
}

static void ucpd_cc_status(int port)
{
	int rc = stm32gx_ucpd_get_role_control(port);
	int cc1_pull, cc2_pull;
	enum tcpc_cc_voltage_status v_cc1, v_cc2;
	int rv;
	char *rp_name;

	cc1_pull = rc & 0x3;
	cc2_pull = (rc >> 2) & 0x3;

	/*
	 * This function is used to display CC settings, including pull type,
	 * and if Rp, what the Rp value is set to. In addition, the current
	 * values of CC voltage detector, polarity, and PD enable status are
	 * displayed.
	 */
	rv = stm32gx_ucpd_get_cc(port, &v_cc1, &v_cc2);
	rp_name = rp_string[(rc >> 4) % 0x3];
	ccprintf("\tcc1\t = %s\n\tcc2\t = %s\n\tRp\t = %s\n", ccx[cc1_pull],
		 ccx[cc2_pull], rp_name);
	if (!rv)
		ccprintf("\tcc1_v\t = %d\n\tcc2_v\t = %d\n", v_cc1, v_cc2);
}

void ucpd_cc_detect_notify_enable(int enable)
{
	/*
	 * This variable is used to control when a CC detach detector is
	 * active.
	 */
	ucpd_cc_change_log = enable;
}

static void ucpd_log_invalidate_entry(void)
{
	/*
	 * This is a msg log utility function which is triggered when an
	 * unexpected detach event is detected.
	 */
	if (msg_log_idx < (MSG_LOG_LEN - 1)) {
		int idx = msg_log_idx;

		msg_log[idx].header = 0xabcd;
		msg_log[idx].ts = __hw_clock_source_read();
		msg_log[idx].dir = 0;
		msg_log[idx].comp = 0;
		msg_log[idx].crc = 0;
		msg_log_cnt++;
		msg_log_idx++;
	}
}

/*
 * This function will mark in the msg log when a detach event occurs. It will
 * only be active if ucpd_cc_change_log is set which can be controlled via the
 * ucpd console command.
 */
static void ucpd_cc_change_notify(void)
{
	if (ucpd_cc_change_log) {
		uint32_t sr = ucpd_sr_cc_event;

		ucpd_log_invalidate_entry();

		ccprintf("vstate: cc1 = %x, cc2 = %x, Rp = %d\n",
			 (sr >> STM32_UCPD_SR_VSTATE_CC1_SHIFT) & 0x3,
			 (sr >> STM32_UCPD_SR_VSTATE_CC2_SHIFT) & 0x3,
			 (ucpd_cc_set_save >> STM32_UCPD_CR_ANASUBMODE_SHIFT) &
				 0x3);
		/* Display CC status on EC console */
		ucpd_cc_status(0);
	}
}
DECLARE_DEFERRED(ucpd_cc_change_notify);
#endif /* CONFIG_STM32G4_UCPD_DEBUG */

static int ucpd_msg_is_good_crc(uint16_t header)
{
	/*
	 * Good CRC is a control message (no data objects) with GOOD_CRC message
	 * type in the header.
	 */
	return ((PD_HEADER_CNT(header) == 0) && (PD_HEADER_EXT(header) == 0) &&
		(PD_HEADER_TYPE(header) == PD_CTRL_GOOD_CRC)) ?
		       1 :
		       0;
}

static void ucpd_hard_reset_rx_log(void)
{
	CPRINTS("ucpd: hard reset recieved");
}
DECLARE_DEFERRED(ucpd_hard_reset_rx_log);

static void ucpd_port_enable(int port, int enable)
{
	if (enable)
		STM32_UCPD_CFGR1(port) |= STM32_UCPD_CFGR1_UCPDEN;
	else
		STM32_UCPD_CFGR1(port) &= ~STM32_UCPD_CFGR1_UCPDEN;
}

static int ucpd_is_cc_pull_active(int port, enum usbpd_cc_pin cc_line)
{
	int cc_enable = (STM32_UCPD_CR(port) & STM32_UCPD_CR_CCENABLE_MASK) >>
			STM32_UCPD_CR_CCENABLE_SHIFT;

	return ((cc_enable >> cc_line) & 0x1);
}

static void ucpd_tx_data_byte(int port)
{
	int index = ucpd_tx_active_buffer->msg_index++;

	STM32_UCPD_TXDR(port) = ucpd_tx_active_buffer->data.msg[index];
}

static void ucpd_rx_data_byte(int port)
{
	if (ucpd_rx_byte_count < UCPD_BUF_LEN)
		ucpd_rx_buffer[ucpd_rx_byte_count++] = STM32_UCPD_RXDR(port);
}

static void ucpd_tx_interrupts_enable(int port, int enable)
{
	if (enable) {
		STM32_UCPD_ICR(port) = UCPD_ICR_TX_INT_MASK;
		STM32_UCPD_IMR(port) |= UCPD_IMR_TX_INT_MASK;
	} else {
		STM32_UCPD_IMR(port) &= ~UCPD_IMR_TX_INT_MASK;
	}
}

static void ucpd_rx_enque_error(void)
{
	CPRINTS("ucpd: TCPM Enque Error!!");
}
DECLARE_DEFERRED(ucpd_rx_enque_error);

static void stm32gx_ucpd_state_init(int port)
{
	/* Init variables used to manage tx process */
	ucpd_tx_request = 0;
	tx_retry_count = 0;
	ucpd_tx_state = STATE_IDLE;
	ucpd_timeout_us = -1;

	/* Init variables used to manage rx */
	ucpd_rx_sop_prime_enabled = 0;
	ucpd_rx_msg_active = 0;
	ucpd_rx_bist_mode = 0;

	/* Vconn tracking variable */
	ucpd_vconn_enable = 0;
}

int stm32gx_ucpd_init(int port)
{
	uint32_t cfgr1_reg;
	uint32_t moder_reg;

	/* Disable UCPD interrupts */
	task_disable_irq(STM32_IRQ_UCPD1);

	/*
	 * After exiting reset, stm32gx will have dead battery mode enabled by
	 * default which connects Rd to CC1/CC2. This should be disabled when EC
	 * is powered up.
	 */
	STM32_PWR_CR3 |= STM32_PWR_CR3_UCPD1_DBDIS;

	/* Ensure that clock to UCPD is enabled */
	STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_UPCD1EN;

	/* Make sure CC1/CC2 pins PB4/PB6 are set for analog mode */
	moder_reg = STM32_GPIO_MODER(GPIO_B);
	moder_reg |= 0x3300;
	STM32_GPIO_MODER(GPIO_B) = moder_reg;
	/*
	 * CFGR1 must be written when UCPD peripheral is disabled. Note that
	 * disabling ucpd causes the peripheral to quit any ongoing activity and
	 * sets all ucpd registers back their default values.
	 */
	ucpd_port_enable(port, 0);

	cfgr1_reg = STM32_UCPD_CFGR1_PSC_CLK_VAL(UCPD_PSC_DIV - 1) |
		    STM32_UCPD_CFGR1_TRANSWIN_VAL(UCPD_TRANSWIN_CNT - 1) |
		    STM32_UCPD_CFGR1_IFRGAP_VAL(UCPD_IFRGAP_CNT - 1) |
		    STM32_UCPD_CFGR1_HBITCLKD_VAL(UCPD_HBIT_DIV - 1);
	STM32_UCPD_CFGR1(port) = cfgr1_reg;

	/*
	 * Set RXORDSETEN field to control which types of ordered sets the PD
	 * receiver must receive.
	 * SOP, SOP', Hard Reset Det, Cable Reset Det enabled
	 */
	STM32_UCPD_CFGR1(port) |= STM32_UCPD_CFGR1_RXORDSETEN_VAL(0x1B);

	/* Enable ucpd  */
	ucpd_port_enable(port, 1);

	/* Configure CC change interrupts */
	STM32_UCPD_IMR(port) = STM32_UCPD_IMR_TYPECEVT1IE |
			       STM32_UCPD_IMR_TYPECEVT2IE;
	STM32_UCPD_ICR(port) = STM32_UCPD_ICR_TYPECEVT1CF |
			       STM32_UCPD_ICR_TYPECEVT2CF;

	/* SOP'/SOP'' must be enabled via TCPCI call */
	ucpd_rx_sop_prime_enabled = false;

	stm32gx_ucpd_state_init(port);

	/* Enable UCPD interrupts */
	task_enable_irq(STM32_IRQ_UCPD1);

	return EC_SUCCESS;
}

int stm32gx_ucpd_release(int port)
{
	ucpd_port_enable(port, 0);

	return EC_SUCCESS;
}

int stm32gx_ucpd_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
			enum tcpc_cc_voltage_status *cc2)
{
	int vstate_cc1;
	int vstate_cc2;
	int anamode;
	uint32_t sr;

	/*
	 * cc_voltage_status is determined from vstate_cc bit field in the
	 * status register. The meaning of the value vstate_cc depends on
	 * current value of ANAMODE (src/snk).
	 *
	 * vstate_cc maps directly to cc_state from tcpci spec when ANAMODE = 1,
	 * but needs to be modified slightly for case ANAMODE = 0.
	 *
	 * If presenting Rp (source), then need to to a circular shift of
	 * vstate_ccx value:
	 *     vstate_cc | cc_state
	 *     ------------------
	 *        0     ->    1
	 *        1     ->    2
	 *        2     ->    0
	 */

	/* Get vstate_ccx values and power role */
	sr = STM32_UCPD_SR(port);
	/* Get Rp or Rd active */
	anamode = !!(STM32_UCPD_CR(port) & STM32_UCPD_CR_ANAMODE);
	vstate_cc1 = (sr & STM32_UCPD_SR_VSTATE_CC1_MASK) >>
		     STM32_UCPD_SR_VSTATE_CC1_SHIFT;
	vstate_cc2 = (sr & STM32_UCPD_SR_VSTATE_CC2_MASK) >>
		     STM32_UCPD_SR_VSTATE_CC2_SHIFT;

	/* Do circular shift if port == source */
	if (anamode) {
		if (vstate_cc1 != STM32_UCPD_SR_VSTATE_RA)
			vstate_cc1 += 4;
		if (vstate_cc2 != STM32_UCPD_SR_VSTATE_RA)
			vstate_cc2 += 4;
	} else {
		if (vstate_cc1 != STM32_UCPD_SR_VSTATE_OPEN)
			vstate_cc1 = (vstate_cc1 + 1) % 3;
		if (vstate_cc2 != STM32_UCPD_SR_VSTATE_OPEN)
			vstate_cc2 = (vstate_cc2 + 1) % 3;
	}

	*cc1 = vstate_cc1;
	*cc2 = vstate_cc2;

	return EC_SUCCESS;
}

int stm32gx_ucpd_get_role_control(int port)
{
	int role_control;
	int cc1;
	int cc2;
	int anamode = !!(STM32_UCPD_CR(port) & STM32_UCPD_CR_ANAMODE);
	int anasubmode =
		(STM32_UCPD_CR(port) & STM32_UCPD_CR_ANASUBMODE_MASK) >>
		STM32_UCPD_CR_ANASUBMODE_SHIFT;

	/*
	 * Role control register is defined as:
	 *     R_cc1 -> b 1:0
	 *     R_cc2 -> b 3:2
	 *     Rp    -> b 5:4
	 *
	 * In TCPCI, CCx is defined as:
	 *    00b -> Ra
	 *    01b -> Rp
	 *    10b -> Rd
	 *    11b -> Open (don't care)
	 *
	 * For ucpd, this information is encoded in ANAMODE and ANASUBMODE
	 * fields as follows:
	 *   ANAMODE            CCx
	 *     0   ->    Rp   -> 1
	 *     1   ->    Rd   -> 2
	 *
	 *   ANASUBMODE:
	 *     00b -> TYPEC_RP_RESERVED (open)
	 *     01b -> TYPEC_RP_USB
	 *     10b -> TYPEC_RP_1A5
	 *     11b -> TYPEC_RP_3A0
	 *
	 *   CCx = ANAMODE + 1, if CCx is enabled
	 *   Rp  = (ANASUBMODE - 1) & 0x3
	 */
	cc1 = ucpd_is_cc_pull_active(port, USBPD_CC_PIN_1) ? anamode + 1 :
							     TYPEC_CC_OPEN;
	cc2 = ucpd_is_cc_pull_active(port, USBPD_CC_PIN_2) ? anamode + 1 :
							     TYPEC_CC_OPEN;
	role_control = cc1 | (cc2 << 2);
	/* Circular shift anasubmode to convert to Rp range */
	role_control |= (UCPD_ANASUB_TO_RP(anasubmode) << 4);

	return role_control;
}

static uint32_t ucpd_get_cc_enable_mask(int port)
{
	uint32_t mask = STM32_UCPD_CR_CCENABLE_MASK;

	if (ucpd_vconn_enable) {
		uint32_t cr = STM32_UCPD_CR(port);
		int pol = !!(cr & STM32_UCPD_CR_PHYCCSEL);

		mask &= ~(1 << (STM32_UCPD_CR_CCENABLE_SHIFT + !pol));
	}

	return mask;
}

int stm32gx_ucpd_vconn_disc_rp(int port, int enable)
{
	int cr;

	/* Update VCONN on/off status. Do this before getting cc enable mask */
	ucpd_vconn_enable = enable;

	cr = STM32_UCPD_CR(port);
	cr &= ~STM32_UCPD_CR_CCENABLE_MASK;
	cr |= ucpd_get_cc_enable_mask(port);

	/* Apply cc pull resistor change */
	STM32_UCPD_CR(port) = cr;

	return EC_SUCCESS;
}

int stm32gx_ucpd_set_cc(int port, int cc_pull, int rp)
{
	uint32_t cr = STM32_UCPD_CR(port);

	/*
	 * Always set ANASUBMODE to match desired Rp. TCPM layer has a valid
	 * range of 0, 1, or 2. This range maps to 1, 2, or 3 in ucpd for
	 * ANASUBMODE.
	 */
	cr &= ~STM32_UCPD_CR_ANASUBMODE_MASK;
	cr |= STM32_UCPD_CR_ANASUBMODE_VAL(UCPD_RP_TO_ANASUB(rp));

	/* Disconnect both pull from both CC lines for R_open case */
	cr &= ~STM32_UCPD_CR_CCENABLE_MASK;
	/* Set ANAMODE if cc_pull is Rd */
	if (cc_pull == TYPEC_CC_RD) {
		cr |= STM32_UCPD_CR_ANAMODE | STM32_UCPD_CR_CCENABLE_MASK;
		/* Clear ANAMODE if cc_pull is Rp */
	} else if (cc_pull == TYPEC_CC_RP) {
		cr &= ~(STM32_UCPD_CR_ANAMODE);
		cr |= ucpd_get_cc_enable_mask(port);
	}

#ifdef CONFIG_STM32G4_UCPD_DEBUG
	if (ucpd_cc_change_log) {
		CPRINTS("ucpd: set_cc: pull = %d, rp = %d", cc_pull, rp);
	}
#endif
	/* Update pull values */
	STM32_UCPD_CR(port) = cr;

	return EC_SUCCESS;
}

int stm32gx_ucpd_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	/*
	 * Polarity impacts the PHYCCSEL, CCENABLE, and CCxTCDIS fields. This
	 * function is called when polarity is updated at TCPM layer. STM32Gx
	 * only supports POLARITY_CC1 or POLARITY_CC2 and this is stored in the
	 * PHYCCSEL bit in the CR register.
	 */
	if (polarity > POLARITY_CC2)
		return EC_ERROR_UNIMPLEMENTED;

	if (polarity == POLARITY_CC1)
		STM32_UCPD_CR(port) &= ~STM32_UCPD_CR_PHYCCSEL;
	else if (polarity == POLARITY_CC2)
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_PHYCCSEL;

#ifdef CONFIG_STM32G4_UCPD_DEBUG
	ucpd_cc_set_save = STM32_UCPD_CR(port);
#endif

	return EC_SUCCESS;
}

int stm32gx_ucpd_set_rx_enable(int port, int enable)
{
	/*
	 * USB PD receiver enable is controlled by the bit PHYRXEN in
	 * UCPD_CR. Enable Rx interrupts when RX PD decoder is active.
	 */
	if (enable) {
		STM32_UCPD_ICR(port) = UCPD_IMR_RX_INT_MASK;
		STM32_UCPD_IMR(port) |= UCPD_IMR_RX_INT_MASK;
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_PHYRXEN;
	} else {
		STM32_UCPD_CR(port) &= ~STM32_UCPD_CR_PHYRXEN;
		STM32_UCPD_IMR(port) &= ~UCPD_IMR_RX_INT_MASK;
	}

	return EC_SUCCESS;
}

int stm32gx_ucpd_set_msg_header(int port, int power_role, int data_role)
{
	msg_header.pr = power_role;
	msg_header.dr = data_role;

	return EC_SUCCESS;
}

int stm32gx_ucpd_sop_prime_enable(int port, bool enable)
{
	/* Update static varialbe used to filter SOP//SOP'' messages */
	ucpd_rx_sop_prime_enabled = enable;

	return EC_SUCCESS;
}

int stm32gx_ucpd_get_chip_info(int port, int live,
			       struct ec_response_pd_chip_info_v1 *chip_info)
{
	chip_info->vendor_id = USB_VID_STM32;
	chip_info->product_id = 0;
	chip_info->device_id = STM32_DBGMCU_IDCODE & 0xfff;
	chip_info->fw_version_number = 0xEC;

	return EC_SUCCESS;
}

static int stm32gx_ucpd_start_transmit(int port, enum ucpd_tx_msg msg_type)
{
	enum tcpci_msg_type type;

	/* Select the correct tx desciptor */
	ucpd_tx_active_buffer = &ucpd_tx_buffers[msg_type];
	type = ucpd_tx_active_buffer->type;

	if (type == TCPCI_MSG_TX_HARD_RESET) {
		/*
		 * From RM0440 45.4.4:
		 * In order to facilitate generation of a Hard Reset, a special
		 * code of TXMODE field is used. No other fields need to be
		 * written. On writing the correct code, the hardware forces
		 * Hard Reset Tx under the correct (optimal) timings with
		 * respect to an on-going Tx message, which (if still in
		 * progress) is cleanly terminated by truncating the current
		 * sequence and directly appending an EOP K-code sequence. No
		 * specific interrupt is generated relating to this truncation
		 * event.
		 *
		 * Because Hard Reset can interrupt ongoing Tx operations, it is
		 * started differently than all other tx messages. Only need to
		 * enable hard reset interrupts, and then set a bit in the CR
		 * register to initiate.
		 */
		/* Enable interrupt for Hard Reset sent/discarded */
		STM32_UCPD_ICR(port) = STM32_UCPD_ICR_HRSTDISCCF |
				       STM32_UCPD_ICR_HRSTSENTCF;
		STM32_UCPD_IMR(port) |= STM32_UCPD_IMR_HRSTDISCIE |
					STM32_UCPD_IMR_HRSTSENTIE;
		/* Initiate Hard Reset */
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_TXHRST;
	} else if (type != TCPCI_MSG_INVALID) {
		int msg_len = 0;
		int mode;

		/*
		 * These types are normal transmission, TXMODE = 0. To transmit
		 * regular message, control or data, requires the following:
		 *     1. Set TXMODE:
		 *          Normal -> 0
		 *          Cable Reset -> 1
		 *          Bist -> 2
		 *     2. Set TX_ORDSETR based on message type
		 *     3. Set TX_PAYSZR which must account for 2 bytes of header
		 *     4. Configure DMA (optional if DMA is desired)
		 *     5. Enable transmit interrupts
		 *     6. Start TX by setting TXSEND in CR
		 *
		 */

		/*
		 * Set tx length parameter (in bytes). Note the count field in
		 * the header is number of 32 bit objects. Also, the length
		 * field must account for the 2 header bytes.
		 */
		if (type == TCPCI_MSG_TX_BIST_MODE_2) {
			mode = STM32_UCPD_CR_TXMODE_BIST;
		} else if (type == TCPCI_MSG_CABLE_RESET) {
			mode = STM32_UCPD_CR_TXMODE_CBL_RST;
		} else {
			mode = STM32_UCPD_CR_TXMODE_DEF;
			msg_len = ucpd_tx_active_buffer->msg_len;
		}

		STM32_UCPD_TX_PAYSZR(port) = msg_len;

		/* Set tx mode */
		STM32_UCPD_CR(port) &= ~STM32_UCPD_CR_TXMODE_MASK;
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_TXMODE_VAL(mode);

		/* Index into ordset enum for start of packet */
		if (type <= TCPCI_MSG_CABLE_RESET)
			STM32_UCPD_TX_ORDSETR(port) = ucpd_txorderset[type];

		/* Reset msg byte index */
		ucpd_tx_active_buffer->msg_index = 0;

		/* Enable interrupts */
		ucpd_tx_interrupts_enable(port, 1);

		/* Trigger ucpd peripheral to start pd message transmit */
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_TXSEND;

#ifdef CONFIG_STM32G4_UCPD_DEBUG
		ucpd_log_add_msg(ucpd_tx_active_buffer->data.header, 0);
#endif
	}

	return EC_SUCCESS;
}

static void ucpd_set_tx_state(enum ucpd_state state)
{
	ucpd_tx_state = state;
}

#ifdef CONFIG_STM32G4_UCPD_DEBUG
static void ucpd_task_log(int timeout, enum ucpd_state enter,
			  enum ucpd_state exit, int req, uint32_t evt)
{
	static int same_count = 0;
	int idx = ucpd_tx_state_log_idx;

	if (ucpd_tx_state_log_freeze)
		return;

	ucpd_tx_statelog[idx].ts = get_time().le.lo;
	ucpd_tx_statelog[idx].tx_request = req;
	ucpd_tx_statelog[idx].timeout_us = timeout;
	ucpd_tx_statelog[idx].enter_state = enter;
	ucpd_tx_statelog[idx].exit_state = exit;
	ucpd_tx_statelog[idx].evt = evt;

	ucpd_tx_state_log_idx = (idx + 1) & TX_STATE_LOG_MASK;

	if (enter == exit) {
		same_count++;
	} else {
		same_count = 0;
	}

	/*
	 * Should not have same enter/exit states. If this happens, then freeze
	 * state log to help in debugging.
	 */
	if (same_count > 5)
		ucpd_tx_state_log_freeze = 1;
}

static void ucpd_task_log_dump(void)
{
	int n;
	int idx;

	ucpd_tx_state_log_freeze = 1;

	/* current index will be oldest entry in the log */
	idx = ucpd_tx_state_log_idx;

	ccprintf("\n\t UCDP Task Log\n");
	for (n = 0; n < TX_STATE_LOG_LEN; n++) {
		ccprintf("[%d]:\t\%8s\t%8s\t%02x\t%08x\t%09d\t%d\n", n,
			 ucpd_names[ucpd_tx_statelog[idx].enter_state],
			 ucpd_names[ucpd_tx_statelog[idx].exit_state],
			 ucpd_tx_statelog[idx].tx_request,
			 ucpd_tx_statelog[idx].evt, ucpd_tx_statelog[idx].ts,
			 ucpd_tx_statelog[idx].timeout_us);

		idx = (idx + 1) & TX_STATE_LOG_MASK;
		crec_msleep(5);
	}

	ucpd_tx_state_log_freeze = 0;
}
#endif

static void ucpd_manage_tx(int port, int evt)
{
	enum ucpd_tx_msg msg_src = TX_MSG_NONE;
	uint16_t hdr;
#ifdef CONFIG_STM32G4_UCPD_DEBUG
	enum ucpd_state enter = ucpd_tx_state;
	int req = ucpd_tx_request;
#endif

	if (evt & UCPD_EVT_HR_REQ) {
		/*
		 * Hard reset control messages are treated as a priority. The
		 * control message will already be set up as it comes from the
		 * PRL layer like any other PD ctrl/data message. So just need
		 * to indicate the correct message source and set the state to
		 * hard reset here.
		 */
		ucpd_set_tx_state(STATE_HARD_RESET);
		msg_src = TX_MSG_TCPM;
		ucpd_tx_request &= ~(1 << msg_src);
	}

	switch (ucpd_tx_state) {
	case STATE_IDLE:
		if (ucpd_tx_request & MSG_GOOD_CRC_MASK) {
			ucpd_set_tx_state(STATE_ACTIVE_CRC);
			msg_src = TX_MSG_GOOD_CRC;
		} else if (ucpd_tx_request & MSG_TCPM_MASK) {
			if (evt & UCPD_EVT_RX_MSG) {
				/*
				 * USB-PD Specification rev 3.0, section 6.10
				 * On receiving a received message, the protocol
				 * layer shall discard any pending message.
				 *
				 * Since the pending message from the PRL has
				 * not been sent yet, it needs to be discarded
				 * based on the received message event.
				 */
				pd_transmit_complete(
					port, TCPC_TX_COMPLETE_DISCARDED);
				ucpd_tx_request &= ~MSG_TCPM_MASK;
			} else if (!ucpd_rx_msg_active) {
				ucpd_set_tx_state(STATE_ACTIVE_TCPM);
				msg_src = TX_MSG_TCPM;
				/* Save msgID required for GoodCRC check */
				hdr = ucpd_tx_buffers[TX_MSG_TCPM].data.header;
				msg_id_match = PD_HEADER_ID(hdr);
				tx_retry_max =
					PD_HEADER_REV(hdr) == PD_REV30 ?
						UCPD_N_RETRY_COUNT_REV30 :
						UCPD_N_RETRY_COUNT_REV20;
			}
		}

		/* If state is not idle, then start tx message */
		if (ucpd_tx_state != STATE_IDLE) {
			ucpd_tx_request &= ~(1 << msg_src);
			tx_retry_count = 0;
		}
		break;

	case STATE_ACTIVE_TCPM:
		/*
		 * Check if tx msg has finsihed. For TCPM messages
		 * transmit is not complete until a GoodCRC message
		 * matching the msgID just sent is received. But, a tx
		 * message can fail due to collision or underrun,
		 * etc. If that failure occurs, dont' wait for GoodCrc
		 * and just go to failure path.
		 */
		if (evt & UCPD_EVT_TX_MSG_SUCCESS) {
			ucpd_set_tx_state(STATE_WAIT_CRC_ACK);
			ucpd_timeout_us = UCPD_T_RECEIVE_US;
		} else if (evt & UCPD_EVT_TX_MSG_DISC ||
			   evt & UCPD_EVT_TX_MSG_FAIL) {
			if (tx_retry_count < tx_retry_max) {
				if (evt & UCPD_EVT_RX_MSG) {
					/*
					 * A message was received so there is no
					 * need to retry this tx message which
					 * had failed to send previously.
					 * Likely, due to the wire
					 * being active from the message that
					 * was just received.
					 */
					ucpd_set_tx_state(STATE_IDLE);
					pd_transmit_complete(
						port,
						TCPC_TX_COMPLETE_DISCARDED);
					ucpd_set_tx_state(STATE_IDLE);
				} else {
					/*
					 * Tx attempt failed. Remain in this
					 * state, but trigger new tx attempt.
					 */
					msg_src = TX_MSG_TCPM;
					tx_retry_count++;
				}
			} else {
				enum tcpc_transmit_complete status;

				status = (evt & UCPD_EVT_TX_MSG_FAIL) ?
						 TCPC_TX_COMPLETE_FAILED :
						 TCPC_TX_COMPLETE_DISCARDED;
				ucpd_set_tx_state(STATE_IDLE);
				pd_transmit_complete(port, status);
			}
		}
		break;

	case STATE_ACTIVE_CRC:
		if (evt & (UCPD_EVT_TX_MSG_SUCCESS | UCPD_EVT_TX_MSG_FAIL |
			   UCPD_EVT_TX_MSG_DISC)) {
			ucpd_set_tx_state(STATE_IDLE);
			if (evt & UCPD_EVT_TX_MSG_FAIL)
				CPRINTS("ucpd: Failed to send GoodCRC!");
			else if (evt & UCPD_EVT_TX_MSG_DISC)
				CPRINTS("ucpd: GoodCRC message discarded!");
		}
		break;

	case STATE_WAIT_CRC_ACK:
		if (evt & UCPD_EVT_RX_GOOD_CRC && ucpd_crc_id == msg_id_match) {
			/* GoodCRC with matching ID was received */
			pd_transmit_complete(port, TCPC_TX_COMPLETE_SUCCESS);
			ucpd_set_tx_state(STATE_IDLE);
#ifdef CONFIG_STM32G4_UCPD_DEBUG
			ucpd_log_mark_crc();
#endif
		} else if ((evt & UCPD_EVT_RX_GOOD_CRC) ||
			   (evt & TASK_EVENT_TIMER)) {
			/* GoodCRC w/out match or timeout waiting */
			if (tx_retry_count < tx_retry_max) {
				ucpd_set_tx_state(STATE_ACTIVE_TCPM);
				msg_src = TX_MSG_TCPM;
				tx_retry_count++;
			} else {
				ucpd_set_tx_state(STATE_IDLE);
				pd_transmit_complete(port,
						     TCPC_TX_COMPLETE_FAILED);
			}
		} else if (evt & UCPD_EVT_RX_MSG) {
			/*
			 * In the case of a collsion, it's possible the port
			 * partner may not send a GoodCRC and instead send the
			 * message that was colliding. If a message is received
			 * in this state, then treat it as a discard from an
			 * incoming message.
			 */
			pd_transmit_complete(port, TCPC_TX_COMPLETE_DISCARDED);
			ucpd_set_tx_state(STATE_IDLE);
		}
		break;

	case STATE_HARD_RESET:
		if (evt & UCPD_EVT_HR_DONE) {
			/* HR complete, reset tx state values */
			ucpd_set_tx_state(STATE_IDLE);
			ucpd_tx_request = 0;
			tx_retry_count = 0;
		} else if (evt & UCPD_EVT_HR_FAIL) {
			ucpd_set_tx_state(STATE_IDLE);
			ucpd_tx_request = 0;
			tx_retry_count = 0;
		}
		break;
	}

	/* If msg_src is valid, then start transmit */
	if (msg_src > TX_MSG_NONE) {
		stm32gx_ucpd_start_transmit(port, msg_src);
	}

#ifdef CONFIG_STM32G4_UCPD_DEBUG
	ucpd_task_log(ucpd_timeout_us, enter, ucpd_tx_state, req, evt);
#endif
}

/*
 * Main task entry point for UCPD task
 *
 * @param p The PD port number for which to handle interrupts (pointer is
 * reinterpreted as an integer directly).
 */
void ucpd_task(void *p)
{
	const int port = (int)((intptr_t)p);

	/* Init variables used to manage tx process */
	stm32gx_ucpd_state_init(port);

	while (1) {
		/*
		 * Note that ucpd_timeout_us is file scope and may be modified
		 * in the tx state machine when entering the STATE_WAIT_CRC_ACK
		 * state. Otherwise, the expectation is that the task is woken
		 * only upon non-timer events.
		 */
		int evt = task_wait_event(ucpd_timeout_us);

		/*
		 * USB-PD messages are intiated in TCPM stack (PRL
		 * layer). However, GoodCRC messages are initiated within the
		 * UCPD driver based on USB-PD rx messages. These 2 types of
		 * transmit paths are managed via task events.
		 *
		 * UCPD generated GoodCRC messages, are the priority path as
		 * they must be sent immediately following a successful USB-PD
		 * rx message. As long as a transmit operation is not underway,
		 * then a transmit message will be started upon request. The ISR
		 * routine sets the event to indicate that the transmit
		 * operation is complete.
		 *
		 * Hard reset requests are sent as a TCPM message, but in terms
		 * of the ucpd transmitter, they are treated as a 3rd tx msg
		 * source since they can interrupt an ongoing tx msg, and there
		 * is no requirement to wait for a GoodCRC reply message.
		 */

		/* Assume there is no timer for next task wake */
		ucpd_timeout_us = -1;

		if (evt & UCPD_EVT_GOOD_CRC_REQ)
			ucpd_tx_request |= MSG_GOOD_CRC_MASK;

		if (evt & UCPD_EVT_TCPM_MSG_REQ)
			ucpd_tx_request |= MSG_TCPM_MASK;

		/*
		 * Manage PD tx messages. The state machine may need to be
		 * called more than once when the task wakes. For instance, if
		 * the task is woken at the completion of sending a GoodCRC,
		 * there may be a TCPM message request pending and just changing
		 * the state back to idle would not trigger start of transmit.
		 */
		do {
			ucpd_manage_tx(port, evt);
			/* Look at task events only once. */
			evt = 0;
		} while (ucpd_tx_request && ucpd_tx_state == STATE_IDLE &&
			 !ucpd_rx_msg_active);
	}
}

static void ucpd_send_good_crc(int port, uint16_t rx_header)
{
	int msg_id;
	int rev_id;
	uint16_t tx_header;
	enum tcpci_msg_type tx_type;
	enum pd_power_role pr = 0;
	enum pd_data_role dr = 0;

	/*
	 * A GoodCRC message shall be sent by receiver to ack that the previous
	 * message was correctly received. The GoodCRC message shall return the
	 * rx message's msg_id field. The one exception is for GoodCRC messages,
	 * which do not generate a GoodCRC response
	 */
	if (ucpd_msg_is_good_crc(rx_header)) {
		return;
	}

	/*
	 * Get the rx ordered set code just detected. SOP -> SOP''_Debug are in
	 * the same order as enum tcpci_msg_type and so can be used
	 * directly.
	 */
	tx_type = STM32_UCPD_RX_ORDSETR(port) & STM32_UCPD_RXORDSETR_MASK;

	/*
	 * PD Header(SOP):
	 *   Extended   b15    -> set to 0 for control messages
	 *   Count      b14:12 -> number of 32 bit data objects = 0 for ctrl msg
	 *   MsgID      b11:9  -> running byte counter (extracted from rx msg)
	 *   Power Role b8     -> stored in static, from set_msg_header()
	 *   Spec Rev   b7:b6  -> PD spec revision (extracted from rx msg)
	 *   Data Role  b5     -> stored in static, from set_msg_header
	 *   Msg Type   b4:b0  -> data or ctrl type = PD_CTRL_GOOD_CRC
	 */
	/* construct header message */
	msg_id = PD_HEADER_ID(rx_header);
	rev_id = PD_HEADER_REV(rx_header);
	if (tx_type == TCPCI_MSG_SOP) {
		pr = msg_header.pr;
		dr = msg_header.dr;
	}
	tx_header = PD_HEADER(PD_CTRL_GOOD_CRC, pr, dr, msg_id, 0, rev_id, 0);

	/* Good CRC is header with no other objects */
	ucpd_tx_buffers[TX_MSG_GOOD_CRC].msg_len = 2;
	ucpd_tx_buffers[TX_MSG_GOOD_CRC].data.header = tx_header;
	ucpd_tx_buffers[TX_MSG_GOOD_CRC].type = tx_type;

	/* Notify ucpd task that a GoodCRC message tx request is pending */
	task_set_event(TASK_ID_UCPD, UCPD_EVT_GOOD_CRC_REQ);
}

int stm32gx_ucpd_transmit(int port, enum tcpci_msg_type type, uint16_t header,
			  const uint32_t *data)
{
	/* Length in bytes = (4 * object len) + 2 header byes */
	int len = (PD_HEADER_CNT(header) << 2) + 2;

	if (len > UCPD_BUF_LEN)
		return EC_ERROR_OVERFLOW;

	/* Store tx msg info in TCPM msg descriptor */
	ucpd_tx_buffers[TX_MSG_TCPM].msg_len = len;
	ucpd_tx_buffers[TX_MSG_TCPM].type = type;
	ucpd_tx_buffers[TX_MSG_TCPM].data.header = header;
	/* Copy msg objects to ucpd data buffer, after 2 header bytes */
	memcpy(ucpd_tx_buffers[TX_MSG_TCPM].data.msg + 2, (uint8_t *)data,
	       len - 2);

	/*
	 * Check for hard reset message here. A different event is used for hard
	 * resets as they are able to interrupt ongoing transmit, and should
	 * have priority over any pending message.
	 */
	if (type == TCPCI_MSG_TX_HARD_RESET)
		task_set_event(TASK_ID_UCPD, UCPD_EVT_HR_REQ);
	else
		task_set_event(TASK_ID_UCPD, UCPD_EVT_TCPM_MSG_REQ);

	return EC_SUCCESS;
}

int stm32gx_ucpd_get_message_raw(int port, uint32_t *payload, int *head)
{
	uint16_t *rx_header = (uint16_t *)ucpd_rx_buffer;
	int rxpaysz;
#ifdef CONFIG_USB_PD_DECODE_SOP
	int sop;
#endif

	/* First 2 bytes of data buffer are the header */
	*head = *rx_header;

#ifdef CONFIG_USB_PD_DECODE_SOP
	/*
	 * The message header is a 16-bit value that's stored in a 32-bit data
	 * type. SOP* is encoded in bits 31 to 28 of the 32-bit data type. NOTE:
	 * The 4 byte header is not part of the PD spec.
	 */
	/* Get SOP value */
	sop = STM32_UCPD_RX_ORDSETR(port) & STM32_UCPD_RXORDSETR_MASK;
	/* Put SOP in bits 31:28 of 32 bit header */
	*head |= PD_HEADER_SOP(sop);
#endif
	rxpaysz = STM32_UCPD_RX_PAYSZR(port) & STM32_UCPD_RX_PAYSZR_MASK;
	/* This size includes 2 bytes for message header */
	rxpaysz -= 2;
	/* Copy payload (src/dst are both 32 bit aligned) */
	memcpy(payload, ucpd_rx_buffer + 2, rxpaysz);

	return EC_SUCCESS;
}

enum ec_error_list stm32gx_ucpd_set_bist_test_mode(const int port,
						   const bool enable)
{
	ucpd_rx_bist_mode = enable;
	CPRINTS("ucpd: Bist test mode = %d", enable);

	return EC_SUCCESS;
}

static void stm32gx_ucpd1_irq(void)
{
	/* STM32_IRQ_UCPD indicates this is from UCPD1, so port = 0 */
	int port = 0;
	uint32_t sr = STM32_UCPD_SR(port);
	uint32_t tx_done_mask = STM32_UCPD_SR_TXMSGSENT |
				STM32_UCPD_SR_TXMSGABT |
				STM32_UCPD_SR_TXMSGDISC |
				STM32_UCPD_SR_HRSTSENT | STM32_UCPD_SR_HRSTDISC;

	/* Check for CC events, set event to wake PD task */
	if (sr & (STM32_UCPD_SR_TYPECEVT1 | STM32_UCPD_SR_TYPECEVT2)) {
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC);
#ifdef CONFIG_STM32G4_UCPD_DEBUG
		ucpd_sr_cc_event = sr;
		hook_call_deferred(&ucpd_cc_change_notify_data, 0);
#endif
	}

	/*
	 * Check for Tx events. tx_mask includes all status bits related to the
	 * end of a USB-PD tx message. If any of these bits are set, the
	 * transmit attempt is completed. Set an event to notify ucpd tx state
	 * machine that transmit operation is complete.
	 */
	if (sr & tx_done_mask) {
		/* Check for tx message complete */
		if (sr & STM32_UCPD_SR_TXMSGSENT) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_TX_MSG_SUCCESS);
#ifdef CONFIG_STM32G4_UCPD_DEBUG
			ucpd_log_mark_tx_comp();
#endif
		} else if (sr &
			   (STM32_UCPD_SR_TXMSGABT | STM32_UCPD_SR_TXUND)) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_TX_MSG_FAIL);
		} else if (sr & STM32_UCPD_SR_TXMSGDISC) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_TX_MSG_DISC);
#ifdef CONFIG_STM32G4_UCPD_DEBUG
			ucpd_log_mark_tx_comp();
#endif
		} else if (sr & STM32_UCPD_SR_HRSTSENT) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_HR_DONE);
		} else if (sr & STM32_UCPD_SR_HRSTDISC) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_HR_FAIL);
		}
		/* Disable Tx interrupts */
		ucpd_tx_interrupts_enable(port, 0);
	}

	/* Check for data register empty */
	if (sr & STM32_UCPD_SR_TXIS)
		ucpd_tx_data_byte(port);

	/* Check for Rx Events */
	/* Check first for start of new message */
	if (sr & STM32_UCPD_SR_RXORDDET) {
		ucpd_rx_byte_count = 0;
		ucpd_rx_msg_active = 1;
	}
	/* Check for byte received */
	if (sr & STM32_UCPD_SR_RXNE)
		ucpd_rx_data_byte(port);

	/* Check for end of message */
	if (sr & STM32_UCPD_SR_RXMSGEND) {
		ucpd_rx_msg_active = 0;
		/* Check for errors */
		if (!(sr & STM32_UCPD_SR_RXERR)) {
			uint16_t *rx_header = (uint16_t *)ucpd_rx_buffer;
			enum tcpci_msg_type type;
			int good_crc = 0;

			type = STM32_UCPD_RX_ORDSETR(port) &
			       STM32_UCPD_RXORDSETR_MASK;

			good_crc = ucpd_msg_is_good_crc(*rx_header);

#ifdef CONFIG_STM32G4_UCPD_DEBUG
			ucpd_log_add_msg(*rx_header, 1);
#endif
			/*
			 * Don't pass GoodCRC control messages to the TCPM
			 * layer. In addition, need to filter for SOP'/SOP''
			 * packets if those are not enabled. SOP'/SOP''
			 * reception is controlled by a static variable. The
			 * hardware orderset detection pattern can't be changed
			 * without disabling the ucpd peripheral.
			 */
			if (!good_crc && (ucpd_rx_sop_prime_enabled ||
					  type == TCPCI_MSG_SOP)) {
				/*
				 * If BIST test mode is active, then still need
				 * to send GoodCRC reply, but there is no need
				 * to send the message up to the tcpm layer.
				 */
				if (!ucpd_rx_bist_mode) {
					if (tcpm_enqueue_message(port))
						hook_call_deferred(
							&ucpd_rx_enque_error_data,
							0);
				}

				task_set_event(TASK_ID_UCPD, UCPD_EVT_RX_MSG);

				/* Send GoodCRC message (if required) */
				ucpd_send_good_crc(port, *rx_header);
			} else if (good_crc) {
				task_set_event(TASK_ID_UCPD,
					       UCPD_EVT_RX_GOOD_CRC);
				ucpd_crc_id = PD_HEADER_ID(*rx_header);
			}
		} else {
			/* Rx message is complete, but there were bit errors */
			CPRINTS("ucpd: rx message error");
		}
	}
	/* Check for fault conditions */
	if (sr & STM32_UCPD_SR_RXHRSTDET) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE);
		hook_call_deferred(&ucpd_hard_reset_rx_log_data, 0);
	}

	/* Clear interrupts now that PD events have been set */
	STM32_UCPD_ICR(port) = sr;
}
DECLARE_IRQ(STM32_IRQ_UCPD1, stm32gx_ucpd1_irq, 1);

#ifdef CONFIG_STM32G4_UCPD_DEBUG
static char ctrl_names[][12] = {
	"rsvd",	   "GoodCRC",	"Goto Min",    "Accept",     "Reject",
	"Ping",	   "PS_Rdy",	"Get_SRC",     "Get_SNK",    "DR_Swap",
	"PR_Swap", "VCONN_Swp", "Wait",	       "Soft_Rst",   "RSVD",
	"RSVD",	   "Not_Sup",	"Get_SRC_Ext", "Get_Status",
};

static char data_names[][10] = {
	"RSVD",	 "SRC_CAP",  "REQUEST",	  "BIST", "SINK_CAP", "BATTERY",
	"ALERT", "GET_INFO", "ENTER_USB", "RSVD", "RSVD",     "RSVD",
	"RSVD",	 "RSVD",     "RSVD",	  "VDM",
};

static void ucpd_dump_msg_log(void)
{
	int i;
	int type;
	int len;
	int dir;
	uint16_t header;
	char *name;

	ccprintf("ucpd: msg_total = %d\n", msg_log_cnt);
	ccprintf("Idx\t  Delta(us)\tDir\t   Type\t\tLen\t s1  s2   PR\t DR\n");
	ccprintf("-----------------------------------------------------------"
		 "-----------------\n");

	for (i = 0; i < msg_log_idx; i++) {
		uint32_t delta_ts = 0;
		int j;

		header = msg_log[i].header;

		if (header != 0xabcd) {
			type = PD_HEADER_TYPE(header);
			len = PD_HEADER_CNT(header);
			name = len ? data_names[type] : ctrl_names[type];
			dir = msg_log[i].dir;
			if (i) {
				delta_ts = msg_log[i].ts - msg_log[i - 1].ts;
			}

			ccprintf("msg[%02d]: %08d\t %s\t %8s\t %02d\t %d  %d\t"
				 "%s\t %s",
				 i, delta_ts, dir ? "Rx" : "Tx", name, len,
				 msg_log[i].comp, msg_log[i].crc,
				 PD_HEADER_PROLE(header) ? "SRC" : "SNK",
				 PD_HEADER_DROLE(header) ? "DFP" : "UFP");
			len = MIN((len * 4) + 2, MSG_BUF_LEN);
			for (j = 0; j < len; j++)
				ccprintf(" %02x", msg_log[i].buf[j]);
		} else {
			if (i) {
				delta_ts = msg_log[i].ts - msg_log[i - 1].ts;
			}
			ccprintf("msg[%02d]: %08d\t CC Voltage Change!", i,
				 delta_ts);
		}
		ccprintf("\n");
		crec_msleep(5);
	}
}

static void stm32gx_ucpd_set_cc_debug(int port, int cc_mask, int pull, int rp)
{
	int cc_enable;
	uint32_t cr = STM32_UCPD_CR(port);

	/*
	 * Only update ANASUBMODE if specified pull type is Rp.
	 */
	if (pull == TYPEC_CC_RP) {
		cr &= ~STM32_UCPD_CR_ANASUBMODE_MASK;
		cr |= STM32_UCPD_CR_ANASUBMODE_VAL(UCPD_RP_TO_ANASUB(rp));
	}

	/*
	 * Can't independently set pull value for CC1 from CC2. But, can
	 * independently connect or disconnect pull for CC1 and CC2. Enable here
	 * the CC lines specified by cc_mask. If desired pull is TYPEC_CC_OPEN,
	 * then the CC lines specified in cc_mask will be disabled.
	 */
	/* Get existing cc enable value */
	cc_enable = (cr & STM32_UCPD_CR_CCENABLE_MASK) >>
		    STM32_UCPD_CR_CCENABLE_SHIFT;
	/* Apply cc_mask (enable CC line specified) */
	cc_enable |= cc_mask;

	/* Set ANAMODE if cc_pull is Rd */
	if (pull == TYPEC_CC_RD)
		cr |= STM32_UCPD_CR_ANAMODE;
	/* Clear ANAMODE if cc_pull is Rp */
	else if (pull == TYPEC_CC_RP)
		cr &= ~(STM32_UCPD_CR_ANAMODE);
	else if (pull == TYPEC_CC_OPEN)
		cc_enable &= ~cc_mask;

	/* The value for this field needs to be OR'd in */
	cr &= ~STM32_UCPD_CR_CCENABLE_MASK;
	cr |= STM32_UCPD_CR_CCENABLE_VAL(cc_enable);
	/* Update pull values */
	STM32_UCPD_CR(port) = cr;
	/* Display updated settings */
	ucpd_cc_status(port);
}

void ucpd_info(int port)
{
	ucpd_cc_status(port);
	ccprintf("\trx_en\t = %d\n\tpol\t = %d\n",
		 !!(STM32_UCPD_CR(port) & STM32_UCPD_CR_PHYRXEN),
		 !!(STM32_UCPD_CR(port) & STM32_UCPD_CR_PHYCCSEL));

	/* Dump ucpd task state info */
	ccprintf("ucpd: tx_state = %s, tx_req = %02x, timeout_us = %d\n",
		 ucpd_names[ucpd_tx_state], ucpd_tx_request, ucpd_timeout_us);

	ucpd_task_log_dump();
}

static int command_ucpd(int argc, const char **argv)
{
	uint32_t tx_data = 0;
	char *e;
	int val;
	int port = 0;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "rst")) {
		/* Force reset of ucpd peripheral */
		stm32gx_ucpd_init(port);
		pd_execute_hard_reset(port);
		task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE);
	} else if (!strcasecmp(argv[1], "info")) {
		ucpd_info(port);
	} else if (!strcasecmp(argv[1], "bist")) {
		/* Need to initiate via DPM to have a timer */
		/* TODO(b/182861002): uncomment when Gingerbread has
		 * full PD support landed.
		 * pd_dpm_request(port, DPM_REQUEST_BIST_TX);
		 */
	} else if (!strcasecmp(argv[1], "hard")) {
		stm32gx_ucpd_transmit(port, TCPCI_MSG_TX_HARD_RESET, 0,
				      &tx_data);
	} else if (!strcasecmp(argv[1], "pol")) {
		if (argc < 3)
			return EC_ERROR_PARAM_COUNT;
		val = strtoi(argv[2], &e, 10);
		if (val > 1)
			val = 0;
		stm32gx_ucpd_set_polarity(port, val);
		stm32gx_ucpd_set_rx_enable(port, 1);
		ccprintf("ucpd: set pol = %d, PHYRXEN = 1\n", val);
	} else if (!strcasecmp(argv[1], "cc")) {
		int cc_mask;
		int pull;
		int rp = 0; /* needs to be initialized */

		if (argc < 3) {
			ucpd_cc_status(port);
			return EC_SUCCESS;
		}
		cc_mask = strtoi(argv[2], &e, 10);
		if (cc_mask < 1 || cc_mask > 3)
			return EC_ERROR_PARAM2;
		/* cc_mask has determines which cc setting to apply */
		if (!strcasecmp(argv[3], "rd")) {
			pull = TYPEC_CC_RD;
		} else if (!strcasecmp(argv[3], "rp")) {
			pull = TYPEC_CC_RP;
			rp = strtoi(argv[4], &e, 10);
			if (rp < 0 || rp > 2)
				return EC_ERROR_PARAM4;
		} else if (!strcasecmp(argv[3], "open")) {
			pull = TYPEC_CC_OPEN;
		} else {
			return EC_ERROR_PARAM3;
		}
		stm32gx_ucpd_set_cc_debug(port, cc_mask, pull, rp);

	} else if (!strcasecmp(argv[1], "log")) {
		if (argc < 3) {
			ucpd_dump_msg_log();
		} else if (!strcasecmp(argv[2], "clr")) {
			msg_log_cnt = 0;
			msg_log_idx = 0;
		}
	} else {
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ucpd, command_ucpd,
			"[rst|info|bist|hard|pol <0|1>|cc xx <rd|rp|open>|log",
			"ucpd peripheral debug and control options");
#endif
