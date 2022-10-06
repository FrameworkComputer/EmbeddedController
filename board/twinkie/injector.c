/* Copyright 2014 The ChromiumOS Authors
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
#include "watchdog.h"

/* FSM command/data buffer */
static uint32_t inj_cmds[INJ_CMD_COUNT];

/* Current polarity for sending operations */
static enum inj_pol inj_polarity = INJ_POL_CC1;

/*
 * CCx Resistors control definition
 *
 * Resistor control GPIOs :
 * CC1_RA       A8
 * CC1_RPUSB    A13
 * CC1_RP1A5    A14
 * CC1_RP3A0    A15
 * CC2_RPUSB    B0
 * CC1_RD       B5
 * CC2_RD       B8
 * CC2_RA       B15
 * CC2_RP1A5    C14
 * CC2_RP3A0    C15
 */
static const struct res_cfg {
	const char *name;
	struct config {
		enum gpio_signal signal;
		uint32_t flags;
	} cfgs[2];
} res_cfg[] = {
	[INJ_RES_NONE] = { "NONE" },
	[INJ_RES_RA] = { "RA",
			 { { GPIO_CC1_RA, GPIO_ODR_LOW },
			   { GPIO_CC2_RA, GPIO_ODR_LOW } } },
	[INJ_RES_RD] = { "RD",
			 { { GPIO_CC1_RD, GPIO_ODR_LOW },
			   { GPIO_CC2_RD, GPIO_ODR_LOW } } },
	[INJ_RES_RPUSB] = { "RPUSB",
			    { { GPIO_CC1_RPUSB, GPIO_OUT_HIGH },
			      { GPIO_CC2_RPUSB, GPIO_OUT_HIGH } } },
	[INJ_RES_RP1A5] = { "RP1A5",
			    { { GPIO_CC1_RP1A5, GPIO_OUT_HIGH },
			      { GPIO_CC2_RP1A5, GPIO_OUT_HIGH } } },
	[INJ_RES_RP3A0] = { "RP3A0",
			    { { GPIO_CC1_RP3A0, GPIO_OUT_HIGH },
			      { GPIO_CC2_RP3A0, GPIO_OUT_HIGH } } },
};

#define CC_RA(cc) (cc < PD_SRC_RD_THRESHOLD)
#define CC_RD(cc) ((cc > PD_SRC_RD_THRESHOLD) && (cc < PD_SRC_VNC))
#define GET_POLARITY(cc1, cc2) (CC_RD(cc2) || CC_RA(cc1))

#ifdef HAS_TASK_SNIFFER
/* we don't have the default DMA handlers */
static void dma_event_interrupt_channel_3(void)
{
	if (STM32_DMA1_REGS->isr & STM32_DMA_ISR_TCIF(STM32_DMAC_CH3)) {
		dma_clear_isr(STM32_DMAC_CH3);
		task_wake(TASK_ID_CONSOLE);
	}
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_2_3, dma_event_interrupt_channel_3, 3);
#endif

static void twinkie_init(void)
{
	/* configure TX clock pins */
	gpio_config_module(MODULE_USB_PD, 1);
	/* Initialize physical layer */
	pd_hw_init(0, PD_ROLE_SINK);
}
DECLARE_HOOK(HOOK_INIT, twinkie_init, HOOK_PRIO_DEFAULT);

/* ------ Helper functions ------ */

static inline int disable_tracing_save(void)
{
	int tr_enabled = STM32_EXTI_IMR & EXTI_COMP_MASK(0);

	if (tr_enabled)
		pd_rx_disable_monitoring(0);
	return tr_enabled;
}

static inline void enable_tracing_ifneeded(int flag)
{
	if (flag)
		pd_rx_enable_monitoring(0);
}

static int send_message(int polarity, uint16_t header, uint8_t cnt,
			const uint32_t *data)
{
	int bit_len;

	/* Don't get preempted by the tracing */
	int flag = disable_tracing_save();

	bit_len = prepare_message(0, header, cnt, data);
	/* Transmit the packet */
	pd_start_tx(0, polarity, bit_len);
	pd_tx_done(0, polarity);

	enable_tracing_ifneeded(flag);

	return bit_len;
}

static int send_hrst(int polarity)
{
	int off;
	int flag = disable_tracing_save();
	/* 64-bit preamble */
	off = pd_write_preamble(0);
	/* Hard-Reset: 3x RST-1 + 1x RST-2 */
	off = pd_write_sym(0, off, 0b0011010101); /* RST-1 = 00111 */
	off = pd_write_sym(0, off, 0b0011010101); /* RST-1 = 00111 */
	off = pd_write_sym(0, off, 0b0011010101); /* RST-1 = 00111 */
	off = pd_write_sym(0, off, 0b0101001101); /* RST-2 = 11001 */
	/* Ensure that we have a final edge */
	off = pd_write_last_edge(0, off);
	/* Transmit the packet */
	pd_start_tx(0, polarity, off);
	pd_tx_done(0, polarity);
	enable_tracing_ifneeded(flag);

	return off;
}

static void set_resistor(int pol, enum inj_res res)
{
	/* reset everything on one CC to high impedance */
	gpio_set_flags(res_cfg[INJ_RES_RA].cfgs[pol].signal, GPIO_ODR_HIGH);
	gpio_set_flags(res_cfg[INJ_RES_RD].cfgs[pol].signal, GPIO_ODR_HIGH);
	gpio_set_flags(res_cfg[INJ_RES_RPUSB].cfgs[pol].signal, GPIO_ODR_HIGH);
	gpio_set_flags(res_cfg[INJ_RES_RP1A5].cfgs[pol].signal, GPIO_ODR_HIGH);
	gpio_set_flags(res_cfg[INJ_RES_RP3A0].cfgs[pol].signal, GPIO_ODR_HIGH);

	/* connect the resistor if needed */
	if (res != INJ_RES_NONE)
		gpio_set_flags(res_cfg[res].cfgs[pol].signal,
			       res_cfg[res].cfgs[pol].flags);
}

static enum inj_pol guess_polarity(enum inj_pol pol)
{
	int cc1_volt, cc2_volt;
	/* polarity forced by the user */
	if (pol == INJ_POL_CC1 || pol == INJ_POL_CC2)
		return pol;
	/* Auto-detection */
	cc1_volt = pd_adc_read(0, 0);
	cc2_volt = pd_adc_read(0, 1);
	return GET_POLARITY(cc1_volt, cc2_volt);
}

/* ------ FSM commands ------ */

static void fsm_send(uint32_t w)
{
	uint16_t header = INJ_ARG0(w);
	int idx = INJ_ARG1(w);
	uint8_t cnt = INJ_ARG2(w);

	/* Buffer overflow */
	if (idx > INJ_CMD_COUNT)
		return;

	send_message(inj_polarity, header, cnt, inj_cmds + idx);
}

static void fsm_wave(uint32_t w)
{
	uint16_t bit_len = INJ_ARG0(w);
	int idx = INJ_ARG1(w);
	int off = 0;
	int nbwords = DIV_ROUND_UP(bit_len, 32);
	int i;
	int flag;

	/* Buffer overflow */
	if (idx + nbwords > INJ_CMD_COUNT)
		return;

	flag = disable_tracing_save();

	for (i = idx; i < idx + nbwords; i++)
		off = encode_word(0, off, inj_cmds[i]);
	/* Ensure that we have a final edge */
	off = pd_write_last_edge(0, bit_len);
	/* Transmit the packet */
	pd_start_tx(0, inj_polarity, off);
	pd_tx_done(0, inj_polarity);
	enable_tracing_ifneeded(flag);
}

static void fsm_wait(uint32_t w)
{
#ifdef HAS_TASK_SNIFFER
	uint32_t timeout_ms = INJ_ARG0(w);
	uint32_t min_edges = INJ_ARG12(w);

	wait_packet(inj_polarity, min_edges, timeout_ms * 1000);
#endif
}

static void fsm_expect(uint32_t w)
{
	uint32_t timeout_ms = INJ_ARG0(w);
	uint8_t cmd = INJ_ARG2(w);

	expect_packet(inj_polarity, cmd, timeout_ms * 1000);
}
static void fsm_get(uint32_t w)
{
	int store_idx = INJ_ARG0(w);
	int param_idx = INJ_ARG1(w);
	uint32_t *store_ptr = inj_cmds + store_idx;

	/* Buffer overflow */
	if (store_idx > INJ_CMD_COUNT)
		return;

	switch (param_idx) {
	case INJ_GET_CC:
		*store_ptr = pd_adc_read(0, 0) | (pd_adc_read(0, 1) << 16);
		break;
	case INJ_GET_VBUS:
		*store_ptr = (ina2xx_get_voltage(0) & 0xffff) |
			     ((ina2xx_get_current(0) & 0xffff) << 16);
		break;
	case INJ_GET_VCONN:
		*store_ptr = (ina2xx_get_voltage(1) & 0xffff) |
			     ((ina2xx_get_current(1) & 0xffff) << 16);
		break;
	case INJ_GET_POLARITY:
		*store_ptr = inj_polarity;
		break;
	default:
		/* Do nothing */
		break;
	}
}

static void fsm_set(uint32_t w)
{
	int val = INJ_ARG0(w);
	int idx = INJ_ARG1(w);

	switch (idx) {
	case INJ_SET_RESISTOR1:
	case INJ_SET_RESISTOR2:
		set_resistor(idx - INJ_SET_RESISTOR1, val);
		break;
	case INJ_SET_RECORD:
#ifdef HAS_TASK_SNIFFER
		recording_enable(val);
#endif
		break;
	case INJ_SET_TX_SPEED:
		pd_set_clock(0, val * 1000);
		break;
	case INJ_SET_RX_THRESH:
		/* set DAC voltage (Vref = 3.3V) */
		STM32_DAC_DHR12RD = val * 4096 / 3300;
		break;
	case INJ_SET_POLARITY:
		inj_polarity = guess_polarity(val);
		break;
	case INJ_SET_TRACE:
		set_trace_mode(val);
		break;
	default:
		/* Do nothing */
		break;
	}
}

static int fsm_run(int index)
{
	while (index < INJ_CMD_COUNT) {
		uint32_t w = inj_cmds[index];
		int cmd = INJ_CMD(w);
		switch (cmd) {
		case INJ_CMD_END:
			return index;
		case INJ_CMD_SEND:
			fsm_send(w);
			break;
		case INJ_CMD_WAVE:
			fsm_wave(w);
			break;
		case INJ_CMD_HRST:
			send_hrst(inj_polarity);
			break;
		case INJ_CMD_WAIT:
			fsm_wait(w);
			break;
		case INJ_CMD_GET:
			fsm_get(w);
			break;
		case INJ_CMD_SET:
			fsm_set(w);
			break;
		case INJ_CMD_JUMP:
			index = INJ_ARG0(w);
			continue; /* do not increment index */
		case INJ_CMD_EXPCT:
			fsm_expect(w);
			break;
		case INJ_CMD_NOP:
		default:
			/* Do nothing */
			break;
		}
		index += 1;
		watchdog_reload();
	}
	return index;
}

/* ------ Console commands ------ */

static int hex8tou32(const char *str, uint32_t *val)
{
	const char *ptr = str;
	uint32_t tmp = 0;

	while (*ptr) {
		char c = *ptr++;
		if (c >= '0' && c <= '9')
			tmp = (tmp << 4) + (c - '0');
		else if (c >= 'A' && c <= 'F')
			tmp = (tmp << 4) + (c - 'A' + 10);
		else if (c >= 'a' && c <= 'f')
			tmp = (tmp << 4) + (c - 'a' + 10);
		else
			return EC_ERROR_INVAL;
	}
	if (ptr != str + 8)
		return EC_ERROR_INVAL;
	*val = tmp;
	return EC_SUCCESS;
}

static int cmd_fsm(int argc, const char **argv)
{
	int index;
	char *e;

	if (argc < 1)
		return EC_ERROR_PARAM2;

	index = strtoi(argv[0], &e, 10);
	if (*e)
		return EC_ERROR_PARAM2;
	index = fsm_run(index);
	ccprintf("FSM Done %d\n", index);

	return EC_SUCCESS;
}

static int cmd_send(int argc, const char **argv)
{
	int pol, cnt, i;
	uint16_t header;
	uint32_t data[VDO_MAX_SIZE];
	char *e;
	int bit_len;

	cnt = argc - 2;
	if (argc < 2 || cnt > VDO_MAX_SIZE)
		return EC_ERROR_PARAM_COUNT;

	pol = strtoi(argv[0], &e, 10) - 1;
	if (*e || pol > 1 || pol < 0)
		return EC_ERROR_PARAM2;
	header = strtoi(argv[1], &e, 16);
	if (*e)
		return EC_ERROR_PARAM3;

	for (i = 0; i < cnt; i++)
		if (hex8tou32(argv[i + 2], data + i))
			return EC_ERROR_INVAL;

	bit_len = send_message(pol, header, cnt, data);
	ccprintf("Sent CC%d %04x + %d = %d\n", pol + 1, header, cnt, bit_len);

	return EC_SUCCESS;
}

static int cmd_cc_level(int argc, const char **argv)
{
	ccprintf("CC1 = %d mV ; CC2 = %d mV\n", pd_adc_read(0, 0),
		 pd_adc_read(0, 1));

	return EC_SUCCESS;
}

static int cmd_resistor(int argc, const char **argv)
{
	int p, r;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	for (p = 0; p < 2; p++) {
		int is_set = 0;
		for (r = 0; r < ARRAY_SIZE(res_cfg); r++)
			if (strcasecmp(res_cfg[r].name, argv[p]) == 0) {
				set_resistor(p, r);
				is_set = 1;
				break;
			}
		/* Unknown name : set to No resistor */
		if (!is_set)
			set_resistor(p, INJ_RES_NONE);
	}
	return EC_SUCCESS;
}

static int cmd_tx_clock(int argc, const char **argv)
{
	int freq;
	char *e;

	if (argc < 1)
		return EC_ERROR_PARAM2;

	freq = strtoi(argv[0], &e, 10);
	if (*e)
		return EC_ERROR_PARAM2;
	pd_set_clock(0, freq);
	ccprintf("TX frequency = %d Hz\n", freq);

	return EC_SUCCESS;
}

static int cmd_rx_threshold(int argc, const char **argv)
{
	int mv;
	char *e;

	if (argc < 1)
		return EC_ERROR_PARAM2;

	mv = strtoi(argv[0], &e, 10);
	if (*e)
		return EC_ERROR_PARAM2;

	/* set DAC voltage (Vref = 3.3V) */
	STM32_DAC_DHR12RD = mv * 4096 / 3300;
	ccprintf("RX threshold = %d mV\n", mv);

	return EC_SUCCESS;
}

static int cmd_ina_dump(int argc, const char **argv, int index)
{
	if (index == 1) { /* VCONN INA is off by default, switch it on */
		ina2xx_write(index, INA2XX_REG_CONFIG, 0x4123);
		/*
		 * wait for the end of conversion : 2x 1.1ms as defined
		 * by the Vb and Vsh CT bits in the CONFIG register above.
		 */
		udelay(2200);
	}

	ccprintf("%s = %d mV ; %d mA\n", index == 0 ? "VBUS" : "VCONN",
		 ina2xx_get_voltage(index), ina2xx_get_current(index));

	if (index == 1) /* power off VCONN INA */
		ina2xx_write(index, INA2XX_REG_CONFIG, 0);

	return EC_SUCCESS;
}

static int cmd_bufwr(int argc, const char **argv)
{
	int idx, cnt, i;
	char *e;

	cnt = argc - 1;
	if (argc < 2 || cnt > INJ_CMD_COUNT)
		return EC_ERROR_PARAM_COUNT;

	idx = strtoi(argv[0], &e, 10);
	if (*e || idx + cnt > INJ_CMD_COUNT)
		return EC_ERROR_PARAM2;

	for (i = 0; i < cnt; i++)
		if (hex8tou32(argv[i + 1], inj_cmds + idx + i))
			return EC_ERROR_INVAL;

	return EC_SUCCESS;
}

static int cmd_bufrd(int argc, const char **argv)
{
	int idx, i;
	int cnt = 1;
	char *e;

	if (argc < 1)
		return EC_ERROR_PARAM_COUNT;

	idx = strtoi(argv[0], &e, 10);
	if (*e || idx > INJ_CMD_COUNT)
		return EC_ERROR_PARAM2;

	if (argc >= 2)
		cnt = strtoi(argv[1], &e, 10);

	if (*e || idx + cnt > INJ_CMD_COUNT)
		return EC_ERROR_PARAM3;

	for (i = idx; i < idx + cnt; i++)
		ccprintf("%08x ", inj_cmds[i]);
	ccprintf("\n");

	return EC_SUCCESS;
}

static int cmd_sink(int argc, const char **argv)
{
	/*
	 * Jump to the RW section which should contain a firmware acting
	 * as a USB PD sink
	 */
	system_run_image_copy(EC_IMAGE_RW);

	return EC_SUCCESS;
}

static int cmd_trace(int argc, const char **argv)
{
	if (argc < 1)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[0], "on") || !strcasecmp(argv[0], "1"))
		set_trace_mode(TRACE_MODE_ON);
	else if (!strcasecmp(argv[0], "raw"))
		set_trace_mode(TRACE_MODE_RAW);
	else if (!strcasecmp(argv[0], "off") || !strcasecmp(argv[0], "0"))
		set_trace_mode(TRACE_MODE_OFF);
	else
		return EC_ERROR_PARAM2;

	return EC_SUCCESS;
}

static int command_tw(int argc, const char **argv)
{
	if (!strcasecmp(argv[1], "send"))
		return cmd_send(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "fsm"))
		return cmd_fsm(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "bufwr"))
		return cmd_bufwr(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "bufrd"))
		return cmd_bufrd(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "cc"))
		return cmd_cc_level(argc - 2, argv + 2);
	else if (!strncasecmp(argv[1], "resistor", 3))
		return cmd_resistor(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "sink"))
		return cmd_sink(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "trace"))
		return cmd_trace(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "txclock"))
		return cmd_tx_clock(argc - 2, argv + 2);
	else if (!strncasecmp(argv[1], "rxthresh", 8))
		return cmd_rx_threshold(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "vbus"))
		return cmd_ina_dump(argc - 2, argv + 2, 0);
	else if (!strcasecmp(argv[1], "vconn"))
		return cmd_ina_dump(argc - 2, argv + 2, 1);
	else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(twinkie, command_tw,
			"[send|fsm|cc|resistor|txclock|rxthresh|vbus|vconn]",
			"Manual Twinkie tweaking");
