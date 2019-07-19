/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "injector.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

/* PD packet text tracing state : TRACE_MODE_OFF/RAW/ON */
int trace_mode;

/* The FSM is waiting for the following command (0 == None) */
uint8_t expected_cmd;

static const char * const ctrl_msg_name[] = {
	[0]                      = "RSVD-C0",
	[PD_CTRL_GOOD_CRC]       = "GOODCRC",
	[PD_CTRL_GOTO_MIN]       = "GOTOMIN",
	[PD_CTRL_ACCEPT]         = "ACCEPT",
	[PD_CTRL_REJECT]         = "REJECT",
	[PD_CTRL_PING]           = "PING",
	[PD_CTRL_PS_RDY]         = "PSRDY",
	[PD_CTRL_GET_SOURCE_CAP] = "GSRCCAP",
	[PD_CTRL_GET_SINK_CAP]   = "GSNKCAP",
	[PD_CTRL_DR_SWAP]        = "DRSWAP",
	[PD_CTRL_PR_SWAP]        = "PRSWAP",
	[PD_CTRL_VCONN_SWAP]     = "VCONNSW",
	[PD_CTRL_WAIT]           = "WAIT",
	[PD_CTRL_SOFT_RESET]     = "SFT-RST",
	[14]                     = "RSVD-C14",
	[15]                     = "RSVD-C15",
};

static const char * const data_msg_name[] = {
	[0]                      = "RSVD-D0",
	[PD_DATA_SOURCE_CAP]     = "SRCCAP",
	[PD_DATA_REQUEST]        = "REQUEST",
	[PD_DATA_BIST]           = "BIST",
	[PD_DATA_SINK_CAP]       = "SNKCAP",
	/* 5-14 Reserved */
	[PD_DATA_VENDOR_DEF]     = "VDM",
};

static const char * const svdm_cmd_name[] = {
	[CMD_DISCOVER_IDENT]     = "DISCID",
	[CMD_DISCOVER_SVID]	 = "DISCSVID",
	[CMD_DISCOVER_MODES]	 = "DISCMODE",
	[CMD_ENTER_MODE]	 = "ENTER",
	[CMD_EXIT_MODE]		 = "EXIT",
	[CMD_ATTENTION]		 = "ATTN",
	[CMD_DP_STATUS]          = "DPSTAT",
	[CMD_DP_CONFIG]          = "DPCFG",
};

static const char * const svdm_cmdt_name[] = {
	[CMDT_INIT]     = "INI",
	[CMDT_RSP_ACK]  = "ACK",
	[CMDT_RSP_NAK]  = "NAK",
	[CMDT_RSP_BUSY] = "BSY",
};

static void print_pdo(uint32_t word)
{
	if ((word & PDO_TYPE_MASK) == PDO_TYPE_BATTERY)
		ccprintf(" %dmV/%dmW", ((word>>10)&0x3ff)*50,
			 (word&0x3ff)*250);
	else
		ccprintf(" %dmV/%dmA", ((word>>10)&0x3ff)*50,
			 (word&0x3ff)*10);
}

static void print_rdo(uint32_t word)
{
	ccprintf("{%d} %08x", RDO_POS(word), word);
}

static void print_vdo(int idx, uint32_t word)
{
	if (idx == 0 && (word & VDO_SVDM_TYPE)) {
		const char *cmd = svdm_cmd_name[PD_VDO_CMD(word)];
		const char *cmdt = svdm_cmdt_name[PD_VDO_CMDT(word)];
		uint16_t vid = PD_VDO_VID(word);
		if (!cmd)
			cmd = "????";
		ccprintf(" V%04x:%s,%s:%08x", vid, cmd, cmdt, word);
	} else {
		ccprintf(" %08x", word);
	}
}

static void print_packet(int head, uint32_t *payload)
{
	int i;
	int cnt = PD_HEADER_CNT(head);
	int typ = PD_HEADER_TYPE(head);
	int id = PD_HEADER_ID(head);
	const char *name;
	const char *prole;

	if (trace_mode == TRACE_MODE_RAW) {
		ccprintf("%pT[%04x]", PRINTF_TIMESTAMP_NOW, head);
		for (i = 0; i < cnt; i++)
			ccprintf(" %08x", payload[i]);
		ccputs("\n");
		return;
	}
	name = cnt ? data_msg_name[typ] : ctrl_msg_name[typ];
	prole = head & (PD_ROLE_SOURCE << 8) ? "SRC" : "SNK";
	ccprintf("%pT %s/%d [%04x]%s",
		 PRINTF_TIMESTAMP_NOW, prole, id, head, name);
	if (!cnt) { /* Control message : we are done */
		ccputs("\n");
		return;
	}
	/* Print payload for data message */
	for (i = 0; i < cnt; i++)
		switch (typ) {
		case PD_DATA_SOURCE_CAP:
		case PD_DATA_SINK_CAP:
			print_pdo(payload[i]);
			break;
		case PD_DATA_REQUEST:
			print_rdo(payload[i]);
			break;
		case PD_DATA_BIST:
			ccprintf("mode %d cnt %04x", payload[i] >> 28,
				 payload[i] & 0xffff);
			break;
		case PD_DATA_VENDOR_DEF:
			print_vdo(i, payload[i]);
			break;
		default:
			ccprintf(" %08x", payload[i]);
	}
	ccputs("\n");
}

static void print_error(enum pd_rx_errors err)
{
	if (err == PD_RX_ERR_INVAL)
		ccprintf("%pT TMOUT\n", PRINTF_TIMESTAMP_NOW);
	else if (err == PD_RX_ERR_HARD_RESET)
		ccprintf("%pT HARD-RST\n", PRINTF_TIMESTAMP_NOW);
	else if (err == PD_RX_ERR_UNSUPPORTED_SOP)
		ccprintf("%pT SOP*\n", PRINTF_TIMESTAMP_NOW);
	else
		ccprintf("ERR %d\n", err);
}

/* keep track of RX edge timing in order to trigger receive */
static timestamp_t rx_edge_ts[2][PD_RX_TRANSITION_COUNT];
static int rx_edge_ts_idx[2];

void rx_event(void)
{
	int pending, i;
	int next_idx;
	pending = STM32_EXTI_PR;

	/* Iterate over the 2 CC lines */
	for (i = 0; i < 2; i++) {
		if (pending & (1 << (21 + i))) {
			rx_edge_ts[i][rx_edge_ts_idx[i]].val = get_time().val;
			next_idx = (rx_edge_ts_idx[i] ==
					PD_RX_TRANSITION_COUNT - 1) ?
						0 : rx_edge_ts_idx[i] + 1;

			/*
			 * If we have seen enough edges in a certain amount of
			 * time, then trigger RX start.
			 */
			if ((rx_edge_ts[i][rx_edge_ts_idx[i]].val -
			     rx_edge_ts[i][next_idx].val)
			     < PD_RX_TRANSITION_WINDOW) {
				/* acquire the message only on the active CC */
				STM32_COMP_CSR &= ~(i ? STM32_COMP_CMP1EN
						      : STM32_COMP_CMP2EN);
				/* start sampling */
				pd_rx_start(0);
				/*
				 * ignore the comparator IRQ until we are done
				 * with current message
				 */
				pd_rx_disable_monitoring(0);
				/* trigger the analysis in the task */
#ifdef HAS_TASK_SNIFFER
				task_set_event(TASK_ID_SNIFFER, 1 << i, 0);
#endif
				/* start reception only one CC line */
				break;
			} else {
				/* do not trigger RX start, just clear int */
				STM32_EXTI_PR = EXTI_COMP_MASK(0);
			}
			rx_edge_ts_idx[i] = next_idx;
		}
	}
}
#ifdef HAS_TASK_SNIFFER
DECLARE_IRQ(STM32_IRQ_COMP, rx_event, 1);
#endif

void trace_packets(void)
{
	int head;
	uint32_t payload[7];

#ifdef HAS_TASK_SNIFFER
	/* Disable sniffer DMA configuration */
	dma_disable(STM32_DMAC_CH6);
	dma_disable(STM32_DMAC_CH7);
	task_disable_irq(STM32_IRQ_DMA_CHANNEL_4_7);
	/* remove TIM1 CH1/2/3 DMA remapping */
	STM32_SYSCFG_CFGR1 &= ~BIT(28);
#endif

	/* "classical" PD RX configuration */
	pd_hw_init_rx(0);
	pd_select_polarity(0, 0);
	/* detect messages on both CCx lines */
	STM32_COMP_CSR |= STM32_COMP_CMP2EN | STM32_COMP_CMP1EN;
	/* Enable the RX interrupts */
	pd_rx_enable_monitoring(0);

	while (1) {
		task_wait_event(-1);
		if (trace_mode == TRACE_MODE_OFF)
			break;
		/* incoming packet processing */
		head = pd_analyze_rx(0, payload);
		pd_rx_complete(0);
		/* re-enabled detection on both CCx lines */
		STM32_COMP_CSR |= STM32_COMP_CMP2EN | STM32_COMP_CMP1EN;
		pd_rx_enable_monitoring(0);
		/* print the last packet content */
		if (head > 0)
			print_packet(head, payload);
		else
			print_error(head);
		if (head > 0 && expected_cmd == PD_HEADER_TYPE(head))
			task_wake(TASK_ID_CONSOLE);
	}

	task_disable_irq(STM32_IRQ_COMP);
	/* Disable tracer DMA configuration */
	dma_disable(STM32_DMAC_CH2);
	/* Put back : sniffer RX hardware configuration */
#ifdef HAS_TASK_SNIFFER
	sniffer_init();
#endif
}

int expect_packet(int pol, uint8_t cmd, uint32_t timeout_us)
{
	uint32_t evt;

	expected_cmd = cmd;
	evt = task_wait_event(timeout_us);

	return !(evt == TASK_EVENT_TIMER);
}

void set_trace_mode(int mode)
{
	/* No change */
	if (mode == trace_mode)
		return;

	trace_mode = mode;
	/* kick the task to take into account the new value */
#ifdef HAS_TASK_SNIFFER
	task_wake(TASK_ID_SNIFFER);
#endif
}
