/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_cros_shi

#include <device.h>
#include <errno.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>
#include <soc.h>
#include <soc_dt.h>
#include <drivers/pinmux.h>
#include <dt-bindings/pinctrl/it8xxx2-pinctrl.h>

#include "chipset.h"
#include "console.h"
#include "host_command.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SPI, format, ## args)

LOG_MODULE_REGISTER(cros_shi, LOG_LEVEL_ERR);

#define DRV_CONFIG(dev) ((struct cros_shi_it8xxx2_cfg * const)(dev)->config)

/*
 * Strcture cros_shi_it8xxx2_cfg is about the setting of SHI,
 * this config will be used at initial time
 */
struct cros_shi_it8xxx2_cfg {
	/* Pinmux control group */
	const struct device *pinctrls;
	/* GPIO pin */
	uint8_t pin;
	/* Alternate function */
	uint8_t alt_fun;
};

#define SPI_RX_MAX_FIFO_SIZE 256
#define SPI_TX_MAX_FIFO_SIZE 256

#define EC_SPI_PREAMBLE_LENGTH 4
#define EC_SPI_PAST_END_LENGTH 4

/* Max data size for a version 3 request/response packet. */
#define SPI_MAX_REQUEST_SIZE SPI_RX_MAX_FIFO_SIZE
#define SPI_MAX_RESPONSE_SIZE (SPI_TX_MAX_FIFO_SIZE - \
	EC_SPI_PREAMBLE_LENGTH - EC_SPI_PAST_END_LENGTH)

static const uint8_t out_preamble[EC_SPI_PREAMBLE_LENGTH] = {
	EC_SPI_PROCESSING,
	EC_SPI_PROCESSING,
	EC_SPI_PROCESSING,
	/* This is the byte which matters */
	EC_SPI_FRAME_START,
};

/* Store read and write data buffer */
static uint8_t in_msg[SPI_RX_MAX_FIFO_SIZE] __aligned(4);
static uint8_t out_msg[SPI_TX_MAX_FIFO_SIZE] __aligned(4);

/* Parameters used by host protocols */
static struct host_packet spi_packet;

enum shi_state_machine {
	/* Ready to receive next request */
	SPI_STATE_READY_TO_RECV,
	/* Receiving request */
	SPI_STATE_RECEIVING,
	/* Processing request */
	SPI_STATE_PROCESSING,
	/* Received bad data */
	SPI_STATE_RX_BAD,

	SPI_STATE_COUNT,
};

static enum shi_state_machine shi_state;

static const int spi_response_state[] = {
	[SPI_STATE_READY_TO_RECV] = EC_SPI_OLD_READY,
	[SPI_STATE_RECEIVING]     = EC_SPI_RECEIVING,
	[SPI_STATE_PROCESSING]    = EC_SPI_PROCESSING,
	[SPI_STATE_RX_BAD]        = EC_SPI_RX_BAD_DATA,
};
BUILD_ASSERT(ARRAY_SIZE(spi_response_state) == SPI_STATE_COUNT);

static void spi_set_state(int state)
{
	/* SPI peripheral state machine */
	shi_state = state;
	/* Response spi peripheral state */
	IT83XX_SPI_SPISRDR = spi_response_state[state];
}

static void reset_rx_fifo(void)
{
	/* End Rx FIFO access */
	IT83XX_SPI_TXRXFAR = 0x00;
	/* Rx FIFO reset and count monitor reset */
	IT83XX_SPI_FCR = IT83XX_SPI_RXFR | IT83XX_SPI_RXFCMR;
}

/* This routine handles spi received unexcepted data */
static void spi_bad_received_data(int count)
{
	/* State machine mismatch, timeout, or protocol we can't handle. */
	spi_set_state(SPI_STATE_RX_BAD);
	/* End CPU access Rx FIFO, so it can clock in bytes from AP again. */
	IT83XX_SPI_TXRXFAR = 0;

	CPRINTS("SPI rx bad data");
	CPRINTF("in_msg=[");
	for (int i = 0; i < count; i++)
		CPRINTF("%02x ", in_msg[i]);
	CPRINTF("]\n");
}

static void spi_response_host_data(uint8_t *out_msg_addr, int tx_size)
{
	/*
	 * Protect sequence of filling response packet for host.
	 * This will ensure CPU access FIFO is disabled at SPI end interrupt no
	 * matter the interrupt is triggered before or after the sequence.
	 */
	unsigned int key = irq_lock();

	if (shi_state == SPI_STATE_PROCESSING) {
		/* Tx FIFO reset and count monitor reset */
		IT83XX_SPI_TXFCR = IT83XX_SPI_TXFR | IT83XX_SPI_TXFCMR;
		/* CPU Tx FIFO1 and FIFO2 access */
		IT83XX_SPI_TXRXFAR = IT83XX_SPI_CPUTFA;

		for (int i = 0; i < tx_size; i += 4) {
			/* Write response data from out_msg buffer to Tx FIFO */
			IT83XX_SPI_CPUWTFDB0 = *(uint32_t *)(out_msg_addr + i);
		}

		/*
		 * After writing data to Tx FIFO is finished, this bit will
		 * be to indicate the SPI peripheral controller.
		 */
		IT83XX_SPI_TXFCR = IT83XX_SPI_TXFS;
		/* End Tx FIFO access */
		IT83XX_SPI_TXRXFAR = 0;
		/* SPI peripheral read Tx FIFO */
		IT83XX_SPI_FCR = IT83XX_SPI_SPISRTXF;
	}

	irq_unlock(key);
}

/*
 * Called to send a response back to the host.
 *
 * Some commands can continue for a while. This function is called by
 * host_command when it completes.
 *
 */
static void spi_send_response_packet(struct host_packet *pkt)
{
	int tx_size;

	if (shi_state != SPI_STATE_PROCESSING) {
		CPRINTS("The request data is not processing.");
		return;
	}

	/* Append our past-end byte, which we reserved space for. */
	for (int i = 0; i < EC_SPI_PAST_END_LENGTH; i++) {
		((uint8_t *)pkt->response)[pkt->response_size + i]
			= EC_SPI_PAST_END;
	}

	tx_size = pkt->response_size + EC_SPI_PREAMBLE_LENGTH +
			EC_SPI_PAST_END_LENGTH;

	/* Transmit the reply */
	spi_response_host_data(out_msg, tx_size);
}

/* Store request data from Rx FIFO to in_msg buffer */
static void spi_host_request_data(uint8_t *in_msg_addr, int count)
{
	/* CPU Rx FIFO1 access */
	IT83XX_SPI_TXRXFAR = IT83XX_SPI_CPURXF1A;
	/*
	 * In spi_parse_header, the request data will separate to
	 * write in_msg buffer so we cannot set CPU to end accessing
	 * Rx FIFO in this function. We will set IT83XX_SPI_TXRXFAR = 0
	 * in reset_rx_fifo.
	 */

	for (int i = 0; i < count; i += 4) {
		/* Get data from master to buffer */
		*(uint32_t *)(in_msg_addr + i) = IT83XX_SPI_RXFRDRB0;
	}
}

/* Parse header for version of spi-protocol */
static void spi_parse_header(void)
{
	struct ec_host_request *r = (struct ec_host_request *)in_msg;

	/* Store request data from Rx FIFO to in_msg buffer */
	spi_host_request_data(in_msg, sizeof(*r));

	/* Protocol version 3 */
	if (in_msg[0] == EC_HOST_REQUEST_VERSION) {
		int pkt_size;

		/* Check how big the packet should be */
		pkt_size = host_request_expected_size(r);

		if (pkt_size == 0 || pkt_size > sizeof(in_msg)) {
			spi_bad_received_data(pkt_size);
			return;
		}

		/* Store request data from Rx FIFO to in_msg buffer */
		spi_host_request_data(in_msg + sizeof(*r),
				      pkt_size - sizeof(*r));

		/* Set up parameters for host request */
		spi_packet.send_response = spi_send_response_packet;
		spi_packet.request = in_msg;
		spi_packet.request_temp = NULL;
		spi_packet.request_max = sizeof(in_msg);
		spi_packet.request_size = pkt_size;

		/* Response must start with the preamble */
		memcpy(out_msg, out_preamble, sizeof(out_preamble));

		spi_packet.response = out_msg + EC_SPI_PREAMBLE_LENGTH;
		/* Reserve space for frame start and trailing past-end byte */
		spi_packet.response_max = SPI_MAX_RESPONSE_SIZE;
		spi_packet.response_size = 0;
		spi_packet.driver_result = EC_RES_SUCCESS;

		/* Go to common-layer to handle request */
		host_packet_receive(&spi_packet);
	} else {
		/* Invalid version number */
		CPRINTS("Invalid version number");
		spi_bad_received_data(1);
	}
}

static void shi_ite_int_handler(const void *arg)
{
	/*
	 * The status of SPI end detection interrupt bit is set, it
	 * means that host command parse has been completed and AP
	 * has received the last byte which is EC_SPI_PAST_END from
	 * EC responded data, then AP ended the transaction.
	 */
	if (IT83XX_SPI_ISR & IT83XX_SPI_ENDDETECTINT) {
		/* Disable CPU access Rx FIFO to clock in data from AP again */
		IT83XX_SPI_TXRXFAR = 0;
		/* Ready to receive */
		spi_set_state(SPI_STATE_READY_TO_RECV);
		/*
		 * Once there is no SPI active, enable idle task deep
		 * sleep bit of SPI in S3 or lower.
		 * TODO(b:185176098): enable_sleep(SLEEP_MASK_SPI);
		 */

		/* CS# is deasserted, so write clear all slave status */
		IT83XX_SPI_ISR = 0xff;
	}
	/*
	 * The status of Rx valid length interrupt bit is set that
	 * indicates reached target count(IT83XX_SPI_FTCB1R,
	 * IT83XX_SPI_FTCB0R) and the length field of the host
	 * requested data.
	 */
	if (IT83XX_SPI_RX_VLISR & IT83XX_SPI_RVLI) {
		/* write clear slave status */
		IT83XX_SPI_RX_VLISR = IT83XX_SPI_RVLI;
		/* Move to processing state */
		spi_set_state(SPI_STATE_PROCESSING);
		/* Parse header for version of spi-protocol */
		spi_parse_header();
	}
}

void spi_event(enum gpio_signal signal)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* Move to processing state */
		spi_set_state(SPI_STATE_PROCESSING);
		/* Disable idle task deep sleep bit of SPI in S0. */
		/* TODO(b:185176098): disable_sleep(SLEEP_MASK_SPI); */
	}
}

/*
 * SHI init priority is behind CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY to
 * overwrite GPIO_INPUT setting of spi chip select pin.
 */
static int cros_shi_ite_init(const struct device *dev)
{
	const struct cros_shi_it8xxx2_cfg *const config = DRV_CONFIG(dev);
	/* Set FIFO data target count */
	struct ec_host_request cmd_head;

	/*
	 * Target count means the size of host request.
	 * And plus extra 4 bytes because the CPU accesses FIFO base on
	 * word. If host requested data length is one byte, we need to
	 * align the data length to 4 bytes.
	 */
	int target_count = sizeof(cmd_head) + 4;
	/* Offset of data_len member of host request. */
	int offset = (char *)&cmd_head.data_len - (char *)&cmd_head;

	IT83XX_SPI_FTCB1R = (target_count >> 8) & 0xff;
	IT83XX_SPI_FTCB0R = target_count & 0xff;
	/*
	 * The register setting can capture the length field of host
	 * request.
	 */
	IT83XX_SPI_TCCB1 = (offset >> 8) & 0xff;
	IT83XX_SPI_TCCB0 = offset & 0xff;

	/*
	 * Memory controller configuration register 3.
	 * bit6 : SPI pin function select (0b:Enable, 1b:Mask)
	 */
	IT83XX_GCTRL_MCCR3 |= IT83XX_GCTRL_SPISLVPFE;
	/* Set unused blocked byte */
	IT83XX_SPI_HPR2 = 0x00;
	/* Rx valid length interrupt enabled */
	IT83XX_SPI_RX_VLISMR &= ~IT83XX_SPI_RVLIM;
	/*
	 * General control register2
	 * bit4 : Rx FIFO2 will not be overwrited once it's full.
	 * bit3 : Rx FIFO1 will not be overwrited once it's full.
	 * bit0 : Rx FIFO1/FIFO2 will reset after each CS_N goes high.
	 */
	IT83XX_SPI_GCR2 = IT83XX_SPI_RXF2OC | IT83XX_SPI_RXF1OC
				| IT83XX_SPI_RXFAR;
	/*
	 * Interrupt mask register (0b:Enable, 1b:Mask)
	 * bit5 : Rx byte reach interrupt mask
	 * bit2 : SPI end detection interrupt mask
	 */
	IT83XX_SPI_IMR &= ~IT83XX_SPI_EDIM;
	/* Reset fifo and prepare to for next transaction */
	reset_rx_fifo();
	/* Ready to receive */
	spi_set_state(SPI_STATE_READY_TO_RECV);
	/* Interrupt status register(write one to clear) */
	IT83XX_SPI_ISR = 0xff;
	/* SPI peripheral controller enable (after settings are ready) */
	IT83XX_SPI_SPISGCR = IT83XX_SPI_SPISCEN;

	/* Ensure spi chip select alt function is enabled. */
	for (int i = 0; i < DT_INST_PROP_LEN(0, pinctrl_0); i++) {
		pinmux_pin_set(config[i].pinctrls, config[i].pin,
			       config[i].alt_fun);
	}

	/* Enable SPI peripheral interrupt */
	IRQ_CONNECT(DT_INST_IRQN(0), 0, shi_ite_int_handler, 0, 0);
	irq_enable(DT_INST_IRQN(0));

	/* Enable SPI chip select pin interrupt */
	gpio_enable_interrupt(GPIO_SPI0_CS);

	return 0;
}

static const struct cros_shi_it8xxx2_cfg cros_shi_cfg[] =
	IT8XXX2_DT_ALT_ITEMS_LIST(0);

#if CONFIG_CROS_SHI_IT8XXX2_INIT_PRIORITY <= \
	CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY
#error "CROS_SHI must initialize after the GPIOs initialization"
#endif
DEVICE_DT_INST_DEFINE(0, cros_shi_ite_init, NULL,
		      NULL, &cros_shi_cfg, POST_KERNEL,
		      CONFIG_CROS_SHI_IT8XXX2_INIT_PRIORITY,
		      NULL);

/* Get protocol information */
enum ec_status spi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = SPI_MAX_REQUEST_SIZE;
	r->max_response_packet_size = SPI_MAX_RESPONSE_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     spi_get_protocol_info,
		     EC_VER_MASK(0));
