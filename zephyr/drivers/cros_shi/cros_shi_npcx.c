/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "host_command.h"
#include "soc_miwu.h"
#include "system.h"

#include <assert.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cmsis_core.h>
#include <drivers/cros_shi.h>
#include <soc.h>
#include <soc/nuvoton_npcx/reg_def_cros.h>

#if DT_HAS_COMPAT_STATUS_OKAY(nuvoton_npcx_shi)
#define DT_DRV_COMPAT nuvoton_npcx_shi
#elif DT_HAS_COMPAT_STATUS_OKAY(nuvoton_npcx_shi_enhanced)
#define DT_DRV_COMPAT nuvoton_npcx_shi_enhanced
#endif
BUILD_ASSERT(!(DT_HAS_COMPAT_STATUS_OKAY(nuvoton_npcx_shi) &&
	       DT_HAS_COMPAT_STATUS_OKAY(nuvoton_npcx_shi_enhanced)));

#ifdef CONFIG_CROS_SHI_NPCX_DEBUG
#define DEBUG_CPRINTS(format, args...) cprints(CC_SPI, format, ##args)
#define DEBUG_CPRINTF(format, args...) cprintf(CC_SPI, format, ##args)
#else
#define DEBUG_CPRINTS(...)
#define DEBUG_CPRINTF(...)
#endif

LOG_MODULE_REGISTER(cros_shi, LOG_LEVEL_DBG);

#define SHI_NODE DT_NODELABEL(shi0)
#define SHI_VER_CTRL_PH DT_PHANDLE_BY_IDX(SHI_NODE, ver_ctrl, 0)
#define SHI_VER_CTRL_ALT_FILED(f) DT_PHA_BY_IDX(SHI_VER_CTRL_PH, alts, 0, f)

/* Full output buffer size */
#define SHI_OBUF_FULL_SIZE 128
/* Full input buffer size  */
#define SHI_IBUF_FULL_SIZE 128
/* Configure the IBUFLVL2 = the size of V3 protocol header */
#define SHI_IBUFLVL2_THRESHOLD (sizeof(struct ec_host_request))
/* Half output buffer size */
#define SHI_OBUF_HALF_SIZE (SHI_OBUF_FULL_SIZE / 2)
/* Half input buffer size */
#define SHI_IBUF_HALF_SIZE (SHI_IBUF_FULL_SIZE / 2)

/*
 * Timeout to wait for SHI request packet
 *
 * This affects the slowest SPI clock we can support. A delay of 8192 us
 * permits a 512-byte request at 500 KHz, assuming the SPI controller starts
 * sending bytes as soon as it asserts chip select. That's as slow as we would
 * practically want to run the SHI interface, since running it slower
 * significantly impacts firmware update times.
 */
#define SHI_CMD_RX_TIMEOUT_US 8192

/*
 * The AP blindly clocks back bytes over the SPI interface looking for a
 * framing byte.  So this preamble must always precede the actual response
 * packet.
 */
#define SHI_OUT_PREAMBLE_LENGTH 2

/*
 * Space allocation of the past-end status byte (EC_SPI_PAST_END) in the out_msg
 * buffer.
 */
#define EC_SPI_PAST_END_LENGTH 1
/*
 * Space allocation of the frame status byte (EC_SPI_FRAME_START) in the out_msg
 * buffer.
 */
#define EC_SPI_FRAME_START_LENGTH 1

/*
 * Offset of output parameters needs to account for pad and framing bytes and
 * one last past-end byte at the end so any additional bytes clocked out by
 * the AP will have a known and identifiable value.
 */
#define SHI_PROTO3_OVERHEAD (EC_SPI_PAST_END_LENGTH + EC_SPI_FRAME_START_LENGTH)

/*
 * Max data size for a version 3 request/response packet.  This is big enough
 * to handle a request/response header, flash write offset/size, and 512 bytes
 * of flash data:
 *  sizeof(ec_host_request):          8
 *  sizeof(ec_params_flash_write):    8
 *  payload                         512
 */
#define SHI_MAX_REQUEST_SIZE CONFIG_CROS_SHI_MAX_REQUEST
#define SHI_MAX_RESPONSE_SIZE CONFIG_CROS_SHI_MAX_RESPONSE

/*
 * Our input and output msg buffers. These must be large enough for our largest
 * message, including protocol overhead.  The pointers after the protocol
 * overhead, as passed to the host command handler, must be 32-bit aligned.
 */
#define SHI_OUT_START_PAD (4 * (EC_SPI_FRAME_START_LENGTH / 4 + 1))
#define SHI_OUT_END_PAD (4 * (EC_SPI_PAST_END_LENGTH / 4 + 1))
static uint8_t out_msg_padded[SHI_OUT_START_PAD + SHI_MAX_RESPONSE_SIZE +
			      SHI_OUT_END_PAD] __aligned(4);
static uint8_t *const out_msg =
	out_msg_padded + SHI_OUT_START_PAD - EC_SPI_FRAME_START_LENGTH;
static uint8_t in_msg[SHI_MAX_REQUEST_SIZE] __aligned(4);

/* Parameters used by host protocols */
static struct host_packet shi_packet;

enum cros_shi_npcx_state {
	SHI_STATE_NONE = -1,
	/* SHI not enabled (initial state, and when chipset is off) */
	SHI_STATE_DISABLED = 0,
	/* Ready to receive next request */
	SHI_STATE_READY_TO_RECV,
	/* Receiving request */
	SHI_STATE_RECEIVING,
	/* Processing request */
	SHI_STATE_PROCESSING,
	/* Canceling response since CS deasserted and output NOT_READY byte */
	SHI_STATE_CNL_RESP_NOT_RDY,
	/* Sending response */
	SHI_STATE_SENDING,
	/* Received data is invalid */
	SHI_STATE_BAD_RECEIVED_DATA,
};

static enum cros_shi_npcx_state state;

/* Device config */
struct cros_shi_npcx_config {
	/* Serial Host Interface (SHI) base address */
	uintptr_t base;
	/* clock configuration */
	struct npcx_clk_cfg clk_cfg;
	/* Pin control configuration */
	const struct pinctrl_dev_config *pcfg;
	/* SHI IRQ */
	int irq;
	struct npcx_wui shi_cs_wui;
};

/* SHI bus parameters */
struct shi_bus_parameters {
	uint8_t *rx_msg; /* Entry pointer of msg rx buffer   */
	uint8_t *tx_msg; /* Entry pointer of msg tx buffer   */
	volatile uint8_t *rx_buf; /* Entry pointer of receive buffer  */
	volatile uint8_t *tx_buf; /* Entry pointer of transmit buffer */
	uint16_t sz_received; /* Size of received data in bytes   */
	uint16_t sz_sending; /* Size of sending data in bytes    */
	uint16_t sz_request; /* request bytes need to receive    */
	uint16_t sz_response; /* response bytes need to receive   */
	uint64_t rx_deadline; /* deadline of receiving            */
} shi_params;

PINCTRL_DT_INST_DEFINE(0);

static const struct cros_shi_npcx_config cros_shi_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.clk_cfg = NPCX_DT_CLK_CFG_ITEM(0),
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
	.irq = DT_INST_IRQN(0),
	.shi_cs_wui = NPCX_DT_WUI_ITEM_BY_NAME(0, shi_cs_wui),
};

struct cros_shi_npcx_data {
	struct host_packet shi_packet;
	sys_slist_t callbacks;
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_shi_npcx_config *)(dev)->config)
#define DRV_DATA(dev) ((struct cros_shi_npcx_data *)(dev)->data)
#define HAL_INSTANCE(dev) (struct shi_reg *)(DRV_CONFIG(dev)->base)

/* Forward declaration */
static void cros_shi_npcx_reset_prepare(struct shi_reg *const inst);

/* Read pointer of input or output buffer by consecutive reading */
static uint32_t shi_read_buf_pointer(struct shi_reg *const inst)
{
	uint8_t stat;

	/* Wait for two consecutive equal values are read */
	do {
		stat = inst->IBUFSTAT;
	} while (stat != inst->IBUFSTAT);

	return (uint32_t)stat;
}

/*
 * Write pointer of output buffer by consecutive reading
 * Note: this function (OBUFSTAT) should only be usd in Enhanced Buffer Mode.
 */
static uint32_t shi_write_buf_pointer(struct shi_reg *const inst)
{
	uint8_t stat;

	/* Wait for two consecutive equal values are read */
	do {
		stat = inst->OBUFSTAT;
	} while (stat != inst->OBUFSTAT);

	return stat;
}

/*
 * Valid offset of SHI output buffer to write.
 * - In Simultaneous Standard FIFO Mode (SIMUL = 1 and EBUFMD = 0):
 *   OBUFPTR cannot be used. IBUFPTR can be used instead because it points to
 *   the same location as OBUFPTR.
 * - In Simultaneous Enhanced FIFO Mode (SIMUL = 1 and EBUFMD = 1):
 *   IBUFPTR may not point to the same location as OBUFPTR.
 *   In this case OBUFPTR reflects the 128-byte payload buffer pointer only
 *   during the SPI transaction.
 */
static uint32_t shi_valid_obuf_offset(struct shi_reg *const inst)
{
	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		return shi_write_buf_pointer(inst) % SHI_OBUF_FULL_SIZE;
	} else {
		return (shi_read_buf_pointer(inst) + SHI_OUT_PREAMBLE_LENGTH) %
		       SHI_OBUF_FULL_SIZE;
	}
}

/*
 * This routine write SHI next half output buffer from msg buffer
 */
static void shi_write_half_outbuf(void)
{
	const uint32_t size =
		MIN(SHI_OBUF_HALF_SIZE,
		    shi_params.sz_response - shi_params.sz_sending);
	uint8_t *obuf_ptr = (uint8_t *)shi_params.tx_buf;
	const uint8_t *obuf_end = obuf_ptr + size;
	uint8_t *msg_ptr = shi_params.tx_msg;

	/* Fill half output buffer */
	while (obuf_ptr != obuf_end)
		*obuf_ptr++ = *msg_ptr++;

	shi_params.sz_sending += size;
	shi_params.tx_buf = obuf_ptr;
	shi_params.tx_msg = msg_ptr;
}

/*
 * This routine read SHI input buffer to msg buffer until
 * we have received a certain number of bytes
 */
static int shi_read_inbuf_wait(struct shi_reg *const inst, uint32_t szbytes)
{
	/* Copy data to msg buffer from input buffer */
	for (uint32_t i = 0; i < szbytes; i++, shi_params.sz_received++) {
		/*
		 * If input buffer pointer equals pointer which wants to read,
		 * it means data is not ready.
		 */
		while (shi_params.rx_buf ==
		       inst->IBUF + shi_read_buf_pointer(inst)) {
			if (k_cycle_get_64() >= shi_params.rx_deadline) {
				return 0;
			}
		}
		/* Restore data to msg buffer */
		*shi_params.rx_msg++ = *shi_params.rx_buf++;
	}
	return 1;
}

/* This routine fills out all SHI output buffer with status byte */
static void shi_fill_out_status(struct shi_reg *const inst, uint8_t status)
{
	uint8_t start, end;
	volatile uint8_t *fill_ptr;
	volatile uint8_t *fill_end;
	volatile uint8_t *obuf_end;

	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		/*
		 * In Enhanced Buffer Mode, SHI module outputs the status code
		 * in SBOBUF repeatedly.
		 */
		inst->SBOBUF = status;

		return;
	}

	/*
	 * Disable interrupts in case the interfere by the other interrupts.
	 * Use __disable_irq/__enable_irq instead of using irq_lock/irq_unlock
	 * here because irq_lock/irq_unlock leave some system exceptions (like
	 * SVC, NMI, and faults) still enabled.
	 */
	__disable_irq();

	/*
	 * Fill out output buffer with status byte and leave a gap for PREAMBLE.
	 * The gap guarantees the synchronization. The critical section should
	 * be done within this gap. No racing happens.
	 */
	start = shi_valid_obuf_offset(inst);
	end = (start + SHI_OBUF_FULL_SIZE - SHI_OUT_PREAMBLE_LENGTH) %
	      SHI_OBUF_FULL_SIZE;

	fill_ptr = inst->OBUF + start;
	fill_end = inst->OBUF + end;
	obuf_end = inst->OBUF + SHI_OBUF_FULL_SIZE;
	while (fill_ptr != fill_end) {
		*fill_ptr++ = status;
		if (fill_ptr == obuf_end)
			fill_ptr = inst->OBUF;
	}

	/* End of critical section */
	__enable_irq();
}

/* This routine handles shi received unexpected data */
static void shi_bad_received_data(struct shi_reg *const inst)
{
	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		inst->EVENABLE &= ~IBF_IBHF_EN_MASK;
	}

	/* State machine mismatch, timeout, or protocol we can't handle. */
	shi_fill_out_status(inst, EC_SPI_RX_BAD_DATA);
	state = SHI_STATE_BAD_RECEIVED_DATA;

	DEBUG_CPRINTF("BAD-");
	DEBUG_CPRINTF("in_msg=[");
	for (uint32_t i = 0; i < shi_params.sz_received; i++)
		DEBUG_CPRINTF("%02x ", in_msg[i]);
	DEBUG_CPRINTF("]\n");

	/* Reset shi's state machine for error recovery */
	cros_shi_npcx_reset_prepare(inst);

	DEBUG_CPRINTF("END\n");
}

/*
 * This routine write SHI output buffer from msg buffer over halt of it.
 * It make sure we have enough time to handle next operations.
 */
static void shi_write_first_pkg_outbuf(struct shi_reg *const inst,
				       uint16_t szbytes)
{
	uint8_t size, offset;
	volatile uint8_t *obuf_ptr;
	volatile uint8_t *obuf_end;
	uint8_t *msg_ptr;
	uint32_t half_buf_remain; /* Remains in half buffer are free to write */

	/* Start writing at our current OBUF position */
	offset = shi_valid_obuf_offset(inst);
	obuf_ptr = inst->OBUF + offset;
	msg_ptr = shi_params.tx_msg;

	/* Fill up to OBUF mid point, or OBUF end */
	half_buf_remain = SHI_OBUF_HALF_SIZE - (offset % SHI_OBUF_HALF_SIZE);
	size = MIN(half_buf_remain, szbytes - shi_params.sz_sending);
	obuf_end = obuf_ptr + size;
	while (obuf_ptr != obuf_end)
		*obuf_ptr++ = *msg_ptr++;

	/* Track bytes sent for later accounting */
	shi_params.sz_sending += size;

	/* Write data to beginning of OBUF if we've reached the end */
	if (obuf_ptr == inst->OBUF + SHI_IBUF_FULL_SIZE)
		obuf_ptr = inst->OBUF;

	/* Fill next half output buffer */
	size = MIN(SHI_OBUF_HALF_SIZE, szbytes - shi_params.sz_sending);
	obuf_end = obuf_ptr + size;
	while (obuf_ptr != obuf_end)
		*obuf_ptr++ = *msg_ptr++;

	/* Track bytes sent / last OBUF position written for later accounting */
	shi_params.sz_sending += size;
	shi_params.tx_buf = obuf_ptr;
	shi_params.tx_msg = msg_ptr;
}

/**
 * Called to send a response back to the host.
 *
 * Some commands can continue for a while. This function is called by
 * host_command task after processing request is completed. It fills up the
 * FIFOs with response package and the remaining data is handled in shi's ISR.
 */
static void shi_send_response_packet(struct host_packet *pkt)
{
	struct shi_reg *const inst = (struct shi_reg *)(cros_shi_cfg.base);

	if (!IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		/*
		 * Disable interrupts. This routine is not called from interrupt
		 * context and buffer underrun will likely occur if it is
		 * preempted after writing its initial reply byte. Also, we must
		 * be sure our state doesn't unexpectedly change, in case we're
		 * expected to take RESP_NOT_RDY actions.
		 */
		__disable_irq();
	}

	if (state == SHI_STATE_PROCESSING) {
		/* Append our past-end byte, which we reserved space for. */
		((uint8_t *)pkt->response)[pkt->response_size] =
			EC_SPI_PAST_END;

		/* Computing sending bytes of response */
		shi_params.sz_response =
			pkt->response_size + SHI_PROTO3_OVERHEAD;

		/* Start to fill output buffer with msg buffer */
		shi_write_first_pkg_outbuf(inst, shi_params.sz_response);
		/* Transmit the reply */
		state = SHI_STATE_SENDING;
		if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
			/*
			 * Enable output buffer half/full empty interrupt and
			 * switch * output mode from repeated single byte mode
			 * to FIFO mode.
			 */
			inst->EVENABLE |= BIT(NPCX_EVENABLE_OBEEN) |
					  BIT(NPCX_EVENABLE_OBHEEN);
			inst->SHICFG6 |= BIT(NPCX_SHICFG6_OBUF_SL);
		}
		DEBUG_CPRINTF("SND-");
	} else if (state == SHI_STATE_CNL_RESP_NOT_RDY) {
		/*
		 * If we're not processing, then the AP has already terminated
		 * the transaction, and won't be listening for a response.
		 * Reset state machine for next transaction.
		 */
		cros_shi_npcx_reset_prepare(inst);
		DEBUG_CPRINTF("END\n");
	} else
		DEBUG_CPRINTS("Unexpected state %d in response handler", state);

	if (!IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		__enable_irq();
	}
}

void shi_handle_host_package(struct shi_reg *const inst)
{
	uint32_t sz_inbuf_int = shi_params.sz_request / SHI_IBUF_HALF_SIZE;
	uint32_t cnt_inbuf_int = shi_params.sz_received / SHI_IBUF_HALF_SIZE;

	if (sz_inbuf_int - cnt_inbuf_int)
		/* Need to receive data from buffer */
		return;
	uint32_t remain_bytes = shi_params.sz_request - shi_params.sz_received;

	/* Read remaining bytes from input buffer */
	if (!shi_read_inbuf_wait(inst, remain_bytes))
		return shi_bad_received_data(inst);

	/* Move to processing state */
	state = SHI_STATE_PROCESSING;
	DEBUG_CPRINTF("PRC-");

	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		inst->EVENABLE &= ~IBF_IBHF_EN_MASK;
	}
	/* Fill output buffer to indicate we`re processing request */
	shi_fill_out_status(inst, EC_SPI_PROCESSING);

	/* Set up parameters for host request */
	shi_packet.send_response = shi_send_response_packet;

	shi_packet.request = in_msg;
	shi_packet.request_temp = NULL;
	shi_packet.request_max = sizeof(in_msg);
	shi_packet.request_size = shi_params.sz_request;

	/* Put FRAME_START in first byte */
	out_msg[0] = EC_SPI_FRAME_START;
	shi_packet.response = out_msg + EC_SPI_FRAME_START_LENGTH;

	/* Reserve space for frame start and trailing past-end byte */
	shi_packet.response_max = SHI_MAX_RESPONSE_SIZE;
	shi_packet.response_size = 0;
	shi_packet.driver_result = EC_RES_SUCCESS;

	/* Go to common layer to handle request */
	host_packet_receive(&shi_packet);
}

static void shi_parse_header(struct shi_reg *const inst)
{
	/* We're now inside a transaction */
	state = SHI_STATE_RECEIVING;
	DEBUG_CPRINTF("RV-");

	/* Setup deadline time for receiving */
	shi_params.rx_deadline =
		k_cycle_get_64() + k_us_to_cyc_near64(SHI_CMD_RX_TIMEOUT_US);

	/* Wait for version, command, length bytes */
	if (!shi_read_inbuf_wait(inst, 3))
		return shi_bad_received_data(inst);

	if (in_msg[0] == EC_HOST_REQUEST_VERSION) {
		/* Protocol version 3 */
		struct ec_host_request *r = (struct ec_host_request *)in_msg;
		int pkt_size;
		/*
		 * If request is over half of input buffer,
		 * we need to modified the algorithm again.
		 */
		__ASSERT_NO_MSG(sizeof(*r) < SHI_IBUF_HALF_SIZE);

		/* Wait for the rest of the command header */
		if (!shi_read_inbuf_wait(inst, sizeof(*r) - 3))
			return shi_bad_received_data(inst);

		/* Check how big the packet should be */
		pkt_size = host_request_expected_size(r);
		if (pkt_size == 0 || pkt_size > sizeof(in_msg))
			return shi_bad_received_data(inst);

		/* Computing total bytes need to receive */
		shi_params.sz_request = pkt_size;

		shi_handle_host_package(inst);
	} else {
		/* Invalid version number */
		return shi_bad_received_data(inst);
	}
}

static void shi_sec_ibf_int_enable(struct shi_reg *const inst, int enable)
{
	if (enable) {
		/* Setup IBUFLVL2 threshold and enable it */
		inst->SHICFG5 |= BIT(NPCX_SHICFG5_IBUFLVL2DIS);
		SET_FIELD(inst->SHICFG5, NPCX_SHICFG5_IBUFLVL2,
			  SHI_IBUFLVL2_THRESHOLD);
		inst->SHICFG5 &= ~BIT(NPCX_SHICFG5_IBUFLVL2DIS);
		/* Enable IBHF2 event */
		inst->EVENABLE2 |= BIT(NPCX_EVENABLE2_IBHF2EN);
	} else {
		/* Disable IBHF2 event first */
		inst->EVENABLE2 &= ~BIT(NPCX_EVENABLE2_IBHF2EN);
		/* Disable IBUFLVL2 and set threshold back to zero */
		inst->SHICFG5 |= BIT(NPCX_SHICFG5_IBUFLVL2DIS);
		SET_FIELD(inst->SHICFG5, NPCX_SHICFG5_IBUFLVL2, 0);
	}
}

/* This routine copies SHI half input buffer data to msg buffer */
static void shi_read_half_inbuf(void)
{
	/*
	 * Copy to read buffer until reaching middle/top address of
	 * input buffer or completing receiving data
	 */
	do {
		/* Restore data to msg buffer */
		*shi_params.rx_msg++ = *shi_params.rx_buf++;
		shi_params.sz_received++;
	} while (shi_params.sz_received % SHI_IBUF_HALF_SIZE &&
		 shi_params.sz_received != shi_params.sz_request);
}

/*
 * Avoid spamming the console with prints every IBF / IBHF interrupt, if
 * we find ourselves in an unexpected state.
 */
static enum cros_shi_npcx_state last_error_state = SHI_STATE_NONE;

static void log_unexpected_state(char *isr_name)
{
	if (state != last_error_state)
		DEBUG_CPRINTF("Unexpected state %d in %s ISR", state, isr_name);
	last_error_state = state;
}

static void shi_handle_cs_assert(struct shi_reg *const inst)
{
	/* If not enabled, ignore glitches on SHI_CS_L */
	if (state == SHI_STATE_DISABLED)
		return;

	/* NOT_READY should be sent and there're no spi transaction now. */
	if (state == SHI_STATE_CNL_RESP_NOT_RDY)
		return;

	/* Chip select is low = asserted */
	if (state != SHI_STATE_READY_TO_RECV) {
		/* State machine should be reset in EVSTAT_EOR ISR */
		DEBUG_CPRINTF("Unexpected state %d in CS ISR", state);
		return;
	}

	DEBUG_CPRINTF("CSL-");

	/*
	 * Clear possible EOR event from previous transaction since it's
	 * irrelevant now that CS is re-asserted.
	 */
	inst->EVSTAT = BIT(NPCX_EVSTAT_EOR);

	/* Do not deep sleep during SHI transaction */
	disable_sleep(SLEEP_MASK_SPI);
}

static void shi_handle_cs_deassert(struct shi_reg *const inst)
{
	/*
	 * If the buffer is still used by the host command.
	 * Change state machine for response handler.
	 */
	if (state == SHI_STATE_PROCESSING) {
		/*
		 * Mark not ready to prevent the other
		 * transaction immediately
		 */
		shi_fill_out_status(inst, EC_SPI_NOT_READY);

		state = SHI_STATE_CNL_RESP_NOT_RDY;

		/*
		 * Disable SHI interrupt, it will remain disabled
		 * until shi_send_response_packet() is called and
		 * CS is asserted for a new transaction.
		 */
		irq_disable(DT_INST_IRQN(0));

		DEBUG_CPRINTF("CNL-");
		return;
		/* Next transaction but we're not ready */
	} else if (state == SHI_STATE_CNL_RESP_NOT_RDY) {
		return;
	}

	/* Error state for checking*/
	if (state != SHI_STATE_SENDING) {
		log_unexpected_state("CSNRE");
	}
	/* reset SHI and prepare to next transaction again */
	cros_shi_npcx_reset_prepare(inst);
	DEBUG_CPRINTF("END\n");
}

static void shi_handle_input_buf_half_full(struct shi_reg *const inst)
{
	if (state == SHI_STATE_RECEIVING) {
		/* Read data from input to msg buffer */
		shi_read_half_inbuf();
		return shi_handle_host_package(inst);
	} else if (state == SHI_STATE_SENDING) {
		/* Write data from msg buffer to output buffer */
		if (shi_params.tx_buf == inst->OBUF + SHI_OBUF_FULL_SIZE) {
			/* Write data from bottom address again */
			shi_params.tx_buf = inst->OBUF;
			return shi_write_half_outbuf();
		} else /* ignore it */
			return;
	} else if (state == SHI_STATE_PROCESSING) {
		/* Wait for host to handle request */
	} else {
		/* Unexpected status */
		log_unexpected_state("IBHF");
	}
}

static void shi_handle_input_buf_full(struct shi_reg *const inst)
{
	if (state == SHI_STATE_RECEIVING) {
		/* read data from input to msg buffer */
		shi_read_half_inbuf();
		/* Read to bottom address again */
		shi_params.rx_buf = inst->IBUF;
		return shi_handle_host_package(inst);
	} else if (state == SHI_STATE_SENDING) {
		/* Write data from msg buffer to output buffer */
		if (shi_params.tx_buf == inst->OBUF + SHI_OBUF_HALF_SIZE)
			return shi_write_half_outbuf();
		else /* ignore it */
			return;
	} else if (state == SHI_STATE_PROCESSING) {
		/* Wait for host to handle request */
		return;
	}
	/* Unexpected status */
	log_unexpected_state("IBF");
}

static void cros_shi_npcx_isr(const struct device *dev)
{
	uint8_t stat;
	uint8_t stat2;
	struct shi_reg *const inst = HAL_INSTANCE(dev);

	/* Read status register and clear interrupt status early */
	stat = inst->EVSTAT;
	inst->EVSTAT = stat;
	stat2 = inst->EVSTAT2;

	/* SHI CS pin is asserted in EVSTAT2 */
	if (IS_BIT_SET(stat2, NPCX_EVSTAT2_CSNFE)) {
		/* Clear pending bit of CSNFE */
		inst->EVSTAT2 = BIT(NPCX_EVSTAT2_CSNFE);
		DEBUG_CPRINTF("CSNFE-");
		/*
		 * BUSY bit is set when SHI_CS is asserted. If not, leave it for
		 * SHI_CS de-asserted event.
		 */
		if (!IS_BIT_SET(inst->SHICFG2, NPCX_SHICFG2_BUSY)) {
			DEBUG_CPRINTF("CSNB-");
			return;
		}
		shi_handle_cs_assert(inst);
	}

	/*
	 * End of data for read/write transaction. i.e. SHI_CS is deasserted.
	 * Host completed or aborted transaction
	 *
	 * EOR has the limitation that it will not be set even if the SHI_CS is
	 * deasserted without SPI clocks. The new SHI module introduce the
	 * CSNRE bit which will be set when SHI_CS is deasserted regardless of
	 * SPI clocks.
	 */
	if (IS_BIT_SET(stat2, NPCX_EVSTAT2_CSNRE)) {
		/* Clear pending bit of CSNRE */
		inst->EVSTAT2 = BIT(NPCX_EVSTAT2_CSNRE);
		/*
		 * We're not in proper state.
		 * Mark not ready to abort next transaction
		 */
		DEBUG_CPRINTF("CSH-");
		return shi_handle_cs_deassert(inst);
	}

	/*
	 * The number of bytes received reaches the size of
	 * protocol V3 header(=8) after CS asserted.
	 */
	if (IS_BIT_SET(stat2, NPCX_EVSTAT2_IBHF2)) {
		/* Clear IBHF2 */
		inst->EVSTAT2 = BIT(NPCX_EVSTAT2_IBHF2);
		DEBUG_CPRINTF("HDR-");
		/* Disable second IBF interrupt and start to parse header */
		shi_sec_ibf_int_enable(inst, 0);
		shi_parse_header(inst);
	}

	/*
	 * Indicate input/output buffer pointer reaches the half buffer size.
	 * Transaction is processing.
	 */
	if (IS_BIT_SET(stat, NPCX_EVSTAT_IBHF)) {
		return shi_handle_input_buf_half_full(inst);
	}

	/*
	 * Indicate input/output buffer pointer reaches the full buffer size.
	 * Transaction is processing.
	 */
	if (IS_BIT_SET(stat, NPCX_EVSTAT_IBF)) {
		return shi_handle_input_buf_full(inst);
	}

	if (IS_BIT_SET(stat, NPCX_EVSTAT_OBE)) {
		return shi_handle_input_buf_full(inst);
	}
	if (IS_BIT_SET(stat, NPCX_EVSTAT_OBHE)) {
		return shi_handle_input_buf_half_full(inst);
	}
}

static void cros_shi_npcx_reset_prepare(struct shi_reg *const inst)
{
	uint32_t i;

	state = SHI_STATE_DISABLED;

	irq_disable(DT_INST_IRQN(0));

	/* Disable SHI unit to clear all status bits */
	inst->SHICFG1 &= ~BIT(NPCX_SHICFG1_EN);

	/* Initialize parameters of next transaction */
	shi_params.rx_msg = in_msg;
	shi_params.tx_msg = out_msg;
	shi_params.rx_buf = inst->IBUF;
	shi_params.tx_buf = inst->IBUF + SHI_OBUF_HALF_SIZE;
	shi_params.sz_received = 0;
	shi_params.sz_sending = 0;
	shi_params.sz_request = 0;
	shi_params.sz_response = 0;

	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		inst->SBOBUF = EC_SPI_RX_READY;
		inst->SBOBUF = EC_SPI_RECEIVING;
		inst->EVENABLE |= IBF_IBHF_EN_MASK;
		inst->EVENABLE &=
			~(BIT(NPCX_EVENABLE_OBEEN) | BIT(NPCX_EVENABLE_OBHEEN));
	} else {
		/*
		 * Fill output buffer to indicate we`re
		 * ready to receive next transaction.
		 */
		for (i = 1; i < SHI_OBUF_FULL_SIZE; i++)
			inst->OBUF[i] = EC_SPI_RECEIVING;
		inst->OBUF[0] = EC_SPI_RX_READY;
	}

	/* SHI/Host Write/input buffer wrap-around enable */
	inst->SHICFG1 = BIT(NPCX_SHICFG1_IWRAP) | BIT(NPCX_SHICFG1_WEN) |
			BIT(NPCX_SHICFG1_EN);

	state = SHI_STATE_READY_TO_RECV;
	last_error_state = SHI_STATE_NONE;

	shi_sec_ibf_int_enable(inst, 1);
	irq_enable(DT_INST_IRQN(0));

	/* Allow deep sleep at the end of SHI transaction */
	enable_sleep(SLEEP_MASK_SPI);

	DEBUG_CPRINTF("RDY-");
}

static int cros_shi_npcx_enable(const struct device *dev)
{
	const struct cros_shi_npcx_config *const config = DRV_CONFIG(dev);
	const struct device *clk_dev = DEVICE_DT_GET(NPCX_CLK_CTRL_NODE);
	struct shi_reg *const inst = HAL_INSTANCE(dev);
	int ret;

	ret = clock_control_on(clk_dev,
			       (clock_control_subsys_t *)&config->clk_cfg);
	if (ret < 0) {
		DEBUG_CPRINTF("Turn on SHI clock fail %d", ret);
		return ret;
	}

	cros_shi_npcx_reset_prepare(inst);
	npcx_miwu_irq_disable(&config->shi_cs_wui);

	/* Configure pin control for SHI */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("cros_shi_npcx pinctrl setup failed (%d)", ret);
		return ret;
	}

	NVIC_ClearPendingIRQ(DT_INST_IRQN(0));
	npcx_miwu_irq_enable(&config->shi_cs_wui);
	irq_enable(DT_INST_IRQN(0));

	return 0;
}

static int cros_shi_npcx_disable(const struct device *dev)
{
	const struct cros_shi_npcx_config *const config = DRV_CONFIG(dev);
	const struct device *clk_dev = DEVICE_DT_GET(NPCX_CLK_CTRL_NODE);
	int ret;

	state = SHI_STATE_DISABLED;

	irq_disable(DT_INST_IRQN(0));
	npcx_miwu_irq_disable(&config->shi_cs_wui);

	/* Configure pin control back to GPIO */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_SLEEP);
	if (ret < 0) {
		LOG_ERR("KB Raw pinctrl setup failed (%d)", ret);
		return ret;
	}

	ret = clock_control_off(clk_dev,
				(clock_control_subsys_t *)&config->clk_cfg);
	if (ret < 0) {
		DEBUG_CPRINTF("Turn off SHI clock fail %d", ret);
		return ret;
	}

	/*
	 * Allow deep sleep again in case CS dropped before ec was
	 * informed in hook function and turn off SHI's interrupt in time.
	 */
	enable_sleep(SLEEP_MASK_SPI);

	return 0;
}

static int shi_npcx_init(const struct device *dev)
{
	int ret;
	const struct cros_shi_npcx_config *const config = DRV_CONFIG(dev);
	struct shi_reg *const inst = HAL_INSTANCE(dev);
	const struct device *clk_dev = DEVICE_DT_GET(NPCX_CLK_CTRL_NODE);

	/* Turn on shi device clock first */
	ret = clock_control_on(clk_dev,
			       (clock_control_subsys_t *)&config->clk_cfg);
	if (ret < 0) {
		DEBUG_CPRINTF("Turn on SHI clock fail %d", ret);
		return ret;
	}

	/* If booter doesn't set the host interface type */
	if (!NPCX_BOOTER_IS_HIF_TYPE_SET()) {
		npcx_host_interface_sel(NPCX_HIF_TYPE_ESPI_SHI);
	}

	/*
	 * SHICFG1 (SHI Configuration 1) setting
	 * [7] - IWRAP	= 1: Wrap input buffer to the first address
	 * [6] - CPOL	= 0: Sampling on rising edge and output on falling edge
	 * [5] - DAS	= 0: return STATUS reg data after Status command
	 * [4] - AUTOBE	= 0: Automatically update the OBES bit in STATUS reg
	 * [3] - AUTIBF	= 0: Automatically update the IBFS bit in STATUS reg
	 * [2] - WEN    = 0: Enable host write to input buffer
	 * [1] - Reserved 0
	 * [0] - ENABLE	= 0: Disable SHI at the beginning
	 */
	inst->SHICFG1 = 0x80;

	/*
	 * SHICFG2 (SHI Configuration 2) setting
	 * [7] - Reserved 0
	 * [6] - REEVEN = 0: Restart events are not used
	 * [5] - Reserved 0
	 * [4] - REEN   = 0: Restart transactions are not used
	 * [3] - SLWU   = 0: Seem-less wake-up is enabled by default
	 * [2] - ONESHOT= 0: WEN is cleared at the end of a write transaction
	 * [1] - BUSY   = 0: SHI bus is busy 0: idle.
	 * [0] - SIMUL	= 1: Turn on simultaneous Read/Write
	 */
	inst->SHICFG2 = 0x01;

	/*
	 * EVENABLE (Event Enable) setting
	 * [7] - IBOREN = 0: Input buffer overrun interrupt enable
	 * [6] - STSREN = 0: status read interrupt disable
	 * [5] - EOWEN  = 0: End-of-Data for Write Transaction Interrupt Enable
	 * [4] - EOREN  = 1: End-of-Data for Read Transaction Interrupt Enable
	 * [3] - IBHFEN = 1: Input Buffer Half Full Interrupt Enable
	 * [2] - IBFEN  = 1: Input Buffer Full Interrupt Enable
	 * [1] - OBHEEN = 0: Output Buffer Half Empty Interrupt Enable
	 * [0] - OBEEN  = 0: Output Buffer Empty Interrupt Enable
	 */
	inst->EVENABLE = 0x1C;

	/*
	 * EVENABLE2 (Event Enable 2) setting
	 * [2] - CSNFEEN = 1: SHI_CS Falling Edge Interrupt Enable
	 * [1] - CSNREEN = 1: SHI_CS Rising Edge Interrupt Enable
	 * [0] - IBHF2EN = 0: Input Buffer Half Full 2 Interrupt Enable
	 */
	inst->EVENABLE2 = 0x06;

	/* Clear SHI events status register */
	inst->EVSTAT = 0xff;

	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_ENHANCED_BUF_MODE)) {
		inst->SHICFG6 |= BIT(NPCX_SHICFG6_EBUFMD);
	}

	npcx_miwu_interrupt_configure(&config->shi_cs_wui, NPCX_MIWU_MODE_EDGE,
				      NPCX_MIWU_TRIG_LOW);
	/* SHI interrupt installation */
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority),
		    cros_shi_npcx_isr, DEVICE_DT_INST_GET(0), 0);

	return ret;
}

static const struct cros_shi_driver_api cros_shi_npcx_driver_api = {
	.enable = cros_shi_npcx_enable,
	.disable = cros_shi_npcx_disable,
};

static struct cros_shi_npcx_data cros_shi_data;
DEVICE_DT_INST_DEFINE(0, shi_npcx_init, /* pm_control_fn= */ NULL,
		      &cros_shi_data, &cros_shi_cfg, PRE_KERNEL_1,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		      &cros_shi_npcx_driver_api);

/* KBS register structure check */
NPCX_REG_SIZE_CHECK(shi_reg, 0x120);
NPCX_REG_OFFSET_CHECK(shi_reg, SHICFG1, 0x001);
NPCX_REG_OFFSET_CHECK(shi_reg, EVENABLE, 0x005);
NPCX_REG_OFFSET_CHECK(shi_reg, IBUFSTAT, 0x00a);
NPCX_REG_OFFSET_CHECK(shi_reg, EVENABLE2, 0x010);
NPCX_REG_OFFSET_CHECK(shi_reg, OBUF, 0x020);
NPCX_REG_OFFSET_CHECK(shi_reg, IBUF, 0x0A0);
