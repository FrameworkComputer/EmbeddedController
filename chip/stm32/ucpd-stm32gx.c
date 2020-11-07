/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32GX UCPD module for Chrome EC */

#include "clock.h"
#include "console.h"
#include "common.h"
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

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
/*
 * UCPD is fed directly from HSI which is @ 16MHz. The ucpd_clk goes to
 * a prescaler who's output feeds the 'half-bit' divider which is used
 * to generate clock for delay counters and BMC Rx/Tx blocks. The rx is
 * designed to work in freq ranges of 6 <--> 18 MHz, however recommended
 * range is 9 <--> 18 MHz.
 *
 *          ------- @ 16 MHz ---------   @ ~600 kHz   -------------
 * HSI ---->| /psc |-------->| /hbit |--------------->| trans_cnt |
 *          -------          ---------    |           -------------
 *                                        |           -------------
 *                                        |---------->| ifrgap_cnt|
 *                                                    -------------
 * Requirements:
 *   1. hbit_clk ~= 600 kHz: 16 MHz / 600 kHz = 26.67
 *   2. tTransitionWindow - 12 to 20 uSec
 *   3. tInterframGap - uSec
 *
 * hbit_clk = HSI_clk / 26 = 615,385 kHz = 1.625 uSec period
 * tTransitionWindow = 1.625 uS * 8 = 13 uS
 * tInterFrameGap = 1.625 uS * 17 = 27.625 uS
 */
#define UCPD_PSC_DIV 1
#define UCPD_HBIT_DIV 27
#define UCPD_TRANSWIN_CNT 8
#define UCPD_IFRGAP_CNT 17

/*
 * USB PD message buffer length. Absent extended messages, the longest PD
 * message will be 7 objects (4 bytes each) plus a 2 byte header. TCPMv2
 * suports extended messages via chunking so the data buffer length is
 * set assumign that extended messages are chunked.
 */
#define UCPD_BUF_LEN 30

#define UCPD_IMR_RX_INT_MASK (STM32_UCPD_IMR_RXNEIE| \
			      STM32_UCPD_IMR_RXORDDETIE | \
			      STM32_UCPD_IMR_RXHRSTDETIE |	\
			      STM32_UCPD_IMR_RXOVRIE |		\
			      STM32_UCPD_IMR_RXMSGENDIE)

#define UCPD_IMR_TX_INT_MASK (STM32_UCPD_IMR_TXISIE | \
			      STM32_UCPD_IMR_TXMSGDISCIE |     \
			      STM32_UCPD_IMR_TXMSGSENTIE |     \
			      STM32_UCPD_IMR_TXMSGABTIE |      \
			      STM32_UCPD_IMR_TXUNDIE)

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
#define UCPD_EVT_GOOD_CRC_REQ   BIT(0)
#define UCPD_EVT_TCPM_MSG_REQ   BIT(1)
#define UCPD_EVT_HR_REQ         BIT(2)
#define UCPD_EVT_TX_MSG_FAIL    BIT(3)
#define UCPD_EVT_TX_MSG_SUCCESS BIT(4)
#define UCPD_EVT_HR_DONE        BIT(5)
#define UCPD_EVT_HR_FAIL        BIT(6)
#define UCPD_EVT_RX_GOOD_CRC    BIT(7)

#define UCPD_T_RECEIVE_US (1 * MSEC)
#ifdef CONFIG_USB_PD_REV30
#define UCPD_N_RETRY_COUNT 2
#else
#define UCPD_N_RETRY_COUNT 3
#endif

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
	enum tcpm_transmit_type type;
	int msg_len;
	int msg_index;
	union buffer data;
};

/* Tx message variables */
struct ucpd_tx_desc ucpd_tx_buffers[TX_MSG_TOTAL];
struct ucpd_tx_desc *ucpd_tx_active_buffer;
static int ucpd_tx_request;
static int ucpd_timeout_us;
static enum ucpd_state ucpd_tx_state;
static int msg_id_match;
static int tx_retry_count;

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

static int ucpd_msg_is_good_crc(uint16_t header)
{
	/*
	 * Good CRC is a control message (no data objects) with GOOD_CRC message
	 * type in the header.
	 */
	return ((PD_HEADER_CNT(header) == 0) && (PD_HEADER_EXT(header) == 0) &&
		(PD_HEADER_TYPE(header) == PD_CTRL_GOOD_CRC)) ? 1 : 0;
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
	int cc_enable = STM32_UCPD_CR(port) & STM32_UCPD_CR_CCENABLE_MASK >>
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
	if (enable)
		STM32_UCPD_IMR(port) |= UCPD_IMR_TX_INT_MASK;
	else
		STM32_UCPD_IMR(port) &= ~UCPD_IMR_TX_INT_MASK;
}

static void ucpd_rx_enque_error(void)
{
	CPRINTS("ucpd: TCPM Enque Error!!");
}
DECLARE_DEFERRED(ucpd_rx_enque_error);

int stm32gx_ucpd_init(int port)
{
	uint32_t cfgr1_reg;
	uint32_t moder_reg;

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
	int anasubmode = (STM32_UCPD_CR(port) & STM32_UCPD_CR_ANASUBMODE_MASK)
		>> STM32_UCPD_CR_ANASUBMODE_SHIFT;

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

	/* Disconnect both pull from both CC lines by default */
	cr &= ~STM32_UCPD_CR_CCENABLE_MASK;
	/* Set ANAMODE if cc_pull is Rd */
	if (cc_pull == TYPEC_CC_RD) {
		cr |= STM32_UCPD_CR_ANAMODE | STM32_UCPD_CR_CCENABLE_MASK;
	/* Clear ANAMODE if cc_pull is Rp */
	} else if (cc_pull == TYPEC_CC_RP) {
		cr &= ~(STM32_UCPD_CR_ANAMODE);
		cr |= STM32_UCPD_CR_CCENABLE_MASK;
	}

	/* Update pull values */
	STM32_UCPD_CR(port) = cr;

	return EC_SUCCESS;
}

int stm32gx_ucpd_set_polarity(int port, enum tcpc_cc_polarity polarity) {
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

	return EC_SUCCESS;
}

int stm32gx_ucpd_set_rx_enable(int port, int enable)
{
	/*
	 * USB PD receiver enable is controlled by the bit PHYRXEN in
	 * UCPD_CR. Enable Rx interrupts when RX PD decoder is active.
	 */
	if (enable) {
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_PHYRXEN;
		STM32_UCPD_ICR(port) |= UCPD_IMR_RX_INT_MASK;
		STM32_UCPD_IMR(port) |= UCPD_IMR_RX_INT_MASK;
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

static int stm32gx_ucpd_start_transmit(int port, enum ucpd_tx_msg msg_type)
{
	enum tcpm_transmit_type type;

	/* Select the correct tx desciptor */
	ucpd_tx_active_buffer = &ucpd_tx_buffers[msg_type];
	type = ucpd_tx_active_buffer->type;

	if (type == TCPC_TX_HARD_RESET) {
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
		STM32_UCPD_IMR(port) |= STM32_UCPD_IMR_HRSTDISCIE |
			STM32_UCPD_IMR_HRSTSENTIE;
		/* Initiate Hard Reset */
		STM32_UCPD_CR(port) |= STM32_UCPD_CR_TXHRST;
	} else if (type != TCPC_TX_INVALID) {
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
		if (type == TCPC_TX_BIST_MODE_2) {
			mode = STM32_UCPD_CR_TXMODE_BIST;
		} else if (type == TCPC_TX_CABLE_RESET) {
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
		if (type <= TCPC_TX_CABLE_RESET )
			STM32_UCPD_TX_ORDSETR(port) = ucpd_txorderset[type];
		else
			STM32_UCPD_TX_ORDSETR(port) =
				ucpd_txorderset[TX_ORDERSET_SOP];

		/* Reset msg byte index */
		ucpd_tx_active_buffer-> msg_index = 0;

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

static void ucpd_manage_tx(int port, int evt)
{
	enum ucpd_tx_msg msg_src = TX_MSG_NONE;

	if (evt & UCPD_EVT_HR_REQ) {
		ucpd_set_tx_state(STATE_HARD_RESET);
		msg_src = MSG_TCPM_MASK;
	}

	switch (ucpd_tx_state) {
	case STATE_IDLE:
		if (ucpd_tx_request & MSG_GOOD_CRC_MASK) {
			ucpd_set_tx_state(STATE_ACTIVE_CRC);
			msg_src = TX_MSG_GOOD_CRC;
		} else if (ucpd_tx_request & MSG_TCPM_MASK) {
			uint16_t hdr;

			ucpd_set_tx_state(STATE_ACTIVE_TCPM);
			msg_src = TX_MSG_TCPM;
			/* Save msgID required for GoodCRC check */
			hdr = ucpd_tx_buffers[TX_MSG_TCPM].data.header;
			msg_id_match = PD_HEADER_ID(hdr);
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
		} else if (evt & UCPD_EVT_TX_MSG_FAIL) {
			if (tx_retry_count < UCPD_N_RETRY_COUNT) {
				/*
				 * Tx attempt failed. Remain in this
				 * state, but trigger new tx attempt.
				 */
				msg_src = TX_MSG_TCPM;
				tx_retry_count++;
			} else {
				ucpd_set_tx_state(STATE_IDLE);
				pd_transmit_complete(
					port, TCPC_TX_COMPLETE_FAILED);
			}
		}
		break;

	case STATE_ACTIVE_CRC:
		if (evt & (UCPD_EVT_TX_MSG_SUCCESS | UCPD_EVT_TX_MSG_FAIL)) {
			ucpd_set_tx_state(STATE_IDLE);
			if (evt & UCPD_EVT_TX_MSG_FAIL)
				CPRINTS("ucpd: Failed to send GoodCRC!");
		}
		break;

	case STATE_WAIT_CRC_ACK:
		if (evt & UCPD_EVT_RX_GOOD_CRC &&
		    ucpd_crc_id == msg_id_match) {
			/* GoodCRC with matching ID was received */
			pd_transmit_complete(port,
					     TCPC_TX_COMPLETE_SUCCESS);
			ucpd_set_tx_state(STATE_IDLE);
#ifdef CONFIG_STM32G4_UCPD_DEBUG
			ucpd_log_mark_crc();
#endif
		} else if ((evt & UCPD_EVT_RX_GOOD_CRC) ||
			   (evt & TASK_EVENT_TIMER)) {
			/* GoodCRC w/out match or timeout waiting */
			if (tx_retry_count < UCPD_N_RETRY_COUNT) {
				ucpd_set_tx_state(STATE_ACTIVE_TCPM);
				msg_src = TX_MSG_TCPM;
				tx_retry_count++;
			} else {
				ucpd_set_tx_state(STATE_IDLE);
				pd_transmit_complete(port,
						     TCPC_TX_COMPLETE_FAILED);
			}
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
}

/*
 * Main task entry point for UCPD task
 *
 * @param p The PD port number for which to handle interrupts (pointer is
 * reinterpreted as an integer directly).
 */
void ucpd_task(void *p)
{
	const int port = (int) ((intptr_t) p);

	/* Init variables used to manage tx process */
	ucpd_tx_request = 0;
	tx_retry_count = 0;
	ucpd_tx_state = STATE_IDLE;
	ucpd_timeout_us = -1;

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
		} while (ucpd_tx_request && ucpd_tx_state == STATE_IDLE);
	}
}

static void ucpd_send_good_crc(int port, uint16_t rx_header)
{
	int msg_id;
	int rev_id;
	uint16_t tx_header;
	enum tcpm_transmit_type tx_type;
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
	 * the same order as enum tcpm_transmit_type and so can be used
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
	if (tx_type == TCPC_TX_SOP) {
		pr = msg_header.pr;
		dr = msg_header.dr;
	}
	tx_header = PD_HEADER(PD_CTRL_GOOD_CRC, pr, dr, msg_id, 0, rev_id, 0);

	/* Good CRC is header with no other objects */
	ucpd_tx_buffers[TX_MSG_GOOD_CRC].msg_len = 2;
	ucpd_tx_buffers[TX_MSG_GOOD_CRC].data.header = tx_header;
	ucpd_tx_buffers[TX_MSG_GOOD_CRC].type = tx_type;

	/* Notify ucpd task that a GoodCRC message tx request is pending */
	task_set_event(TASK_ID_UCPD, UCPD_EVT_GOOD_CRC_REQ, 0);
}

int stm32gx_ucpd_transmit(int port,
			  enum tcpm_transmit_type type,
			  uint16_t header,
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

	/* Notify ucpd task that a TCPM message tx request is pending */
	task_set_event(TASK_ID_UCPD, UCPD_EVT_TCPM_MSG_REQ, 0);

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
 * The message header is a 16-bit value that's stored in a 32-bit data type.
 * SOP* is encoded in bits 31 to 28 of the 32-bit data type.
 * NOTE: The 4 byte header is not part of the PD spec.
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

void stm32gx_ucpd1_irq(void)
{
	/* STM32_IRQ_UCPD indicates this is from UCPD1, so port = 0 */
	int port = 0;
	uint32_t sr = STM32_UCPD_SR(port);
	uint32_t tx_done_mask = STM32_UCPD_SR_TXMSGSENT | STM32_UCPD_SR_TXMSGABT
		| STM32_UCPD_SR_TXMSGDISC | STM32_UCPD_SR_HRSTSENT |
		STM32_UCPD_SR_HRSTDISC;

	/* Check for CC events, set event to wake PD task */
	if (sr & (STM32_UCPD_SR_TYPECEVT1 | STM32_UCPD_SR_TYPECEVT2))
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);

	/*
	 * Check for Tx events. tx_mask includes all status bits related to the
	 * end of a USB-PD tx message. If any of these bits are set, the
	 * transmit attempt is completed. Set an event to notify ucpd tx state
	 * machine that transmit operation is complete.
	 */
	if (sr & tx_done_mask) {
		/* Check for tx message complete */
		if (sr & STM32_UCPD_SR_TXMSGSENT) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_TX_MSG_SUCCESS,
				       0);
#ifdef CONFIG_STM32G4_UCPD_DEBUG
			ucpd_log_mark_tx_comp();
#endif
		} else if (sr & (STM32_UCPD_SR_TXMSGABT |
			       STM32_UCPD_SR_TXMSGDISC |STM32_UCPD_SR_TXUND)) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_TX_MSG_FAIL, 0);
		} else if (sr & STM32_UCPD_SR_HRSTSENT) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_HR_DONE, 0);
		} else if (sr & STM32_UCPD_SR_HRSTDISC) {
			task_set_event(TASK_ID_UCPD, UCPD_EVT_HR_FAIL, 0);
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
	}
	/* Check for byte received */
	if (sr & STM32_UCPD_SR_RXNE)
		ucpd_rx_data_byte(port);

	/* Check for end of message */
	if (sr & STM32_UCPD_SR_RXMSGEND) {
		/* Check for errors */
		if (!(sr & STM32_UCPD_SR_RXERR)) {
			int rv;
			uint16_t *rx_header = (uint16_t *)ucpd_rx_buffer;

			/* Don't pass GoodCRC control messages TCPM */
			if (!ucpd_msg_is_good_crc(*rx_header)) {
				/* TODO - Add error checking here */
				rv = tcpm_enqueue_message(port);
				if (rv)
					hook_call_deferred(&ucpd_rx_enque_error_data,
							   0);
				/* Send GoodCRC message (if required) */
				ucpd_send_good_crc(port, *rx_header);
			} else {
				task_set_event(TASK_ID_UCPD,
						       UCPD_EVT_RX_GOOD_CRC, 0);
				ucpd_crc_id = PD_HEADER_ID(*rx_header);
			}
		}
	}
	/* Check for fault conditions */
	if (sr & STM32_UCPD_SR_RXHRSTDET) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE, 0);
		hook_call_deferred(&ucpd_hard_reset_rx_log_data, 0);
	}

	/* Clear interrupts now that PD events have been set */
	STM32_UCPD_ICR(port) = sr;
}
DECLARE_IRQ(STM32_IRQ_UCPD1, stm32gx_ucpd1_irq, 1);
