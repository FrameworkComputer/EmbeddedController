/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "usb_pd.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(tcpci_src_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

/**
 * @brief Start source capability timer. Capability message will be send after
 *        @p time.
 *
 * @param data Pointer to USB-C source device emulator data
 * @param time Time to delay before sending capability message
 */
static void tcpci_src_emul_start_source_capability_custom_time(
	struct tcpci_src_emul_data *data, k_timeout_t time)
{
	/* Use reschedule */
	k_work_reschedule(&data->source_capability_timeout, time);
}

/**
 * @brief Start source capability timer. Capability message will be send after
 *        TCPCI_SOURCE_CAPABILITY_TIMEOUT milliseconds.
 *
 * @param data Pointer to USB-C source device emulator data
 */
static void
tcpci_src_emul_start_source_capability_timer(struct tcpci_src_emul_data *data)
{
	tcpci_src_emul_start_source_capability_custom_time(
		data, TCPCI_SOURCE_CAPABILITY_TIMEOUT);
}

/**
 * @brief Stop source capability timer. Capability message will not be repeated.
 *
 * @param data Pointer to USB-C source device emulator data
 */
static void
tcpci_src_emul_stop_source_capability_timer(struct tcpci_src_emul_data *data)
{
	k_work_cancel_delayable(&data->source_capability_timeout);
}

int tcpci_src_emul_send_capability_msg(struct tcpci_src_emul_data *data,
				       struct tcpci_partner_data *common_data,
				       uint64_t delay)
{
	int pdos;

	/* Find number of PDOs */
	for (pdos = 0; pdos < PDO_MAX_OBJECTS; pdos++) {
		if (data->pdo[pdos] == 0) {
			break;
		}
	}

	return tcpci_partner_send_data_msg(common_data, PD_DATA_SOURCE_CAP,
					   data->pdo, pdos, delay);
}

int tcpci_src_emul_send_capability_msg_with_timer(
	struct tcpci_src_emul_data *data,
	struct tcpci_partner_data *common_data, uint64_t delay)
{
	int ret;

	if (delay > 0) {
		tcpci_src_emul_start_source_capability_custom_time(
			data, K_MSEC(delay));
		return TCPCI_EMUL_TX_SUCCESS;
	}

	ret = tcpci_src_emul_send_capability_msg(data, common_data, 0);

	if (ret != TCPCI_EMUL_TX_SUCCESS) {
		tcpci_src_emul_start_source_capability_timer(data);
	} else {
		/* Expect Request message before SenderResponse timeout */
		tcpci_partner_start_sender_response_timer(common_data);
		/* Do not expect Accept or Reject messages */
		common_data->wait_for_response = false;
	}

	return TCPCI_EMUL_TX_SUCCESS;
}

void tcpci_src_emul_clear_alert_received(struct tcpci_src_emul_data *data)
{
	data->alert_received = false;
}

void tcpci_src_emul_clear_status_received(struct tcpci_src_emul_data *data)
{
	data->status_received = false;
}

/**
 * @brief Handle SOP messages as TCPCI source device. It handles request
 *        and get source cap messages.
 *
 * @param ext Pointer to USB-C source device emulator extension
 * @param common_data Pointer to USB-C device emulator common data
 * @param msg Pointer to received message
 *
 * @param TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled
 * @param TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 */
static enum tcpci_partner_handler_res
tcpci_src_emul_handle_sop_msg(struct tcpci_partner_extension *ext,
			      struct tcpci_partner_data *common_data,
			      const struct tcpci_emul_msg *msg)
{
	struct tcpci_src_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_src_emul_data, ext);
	uint16_t header;

	header = sys_get_le16(msg->buf);

	if (PD_HEADER_EXT(header)) {
		/* Handle extended message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_EXT_STATUS:
			data->status_received = true;
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	} else if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_REQUEST:
			tcpci_partner_stop_sender_response_timer(common_data);
			/* TODO(b/224925855): Validate if request can be met */
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_ACCEPT, 0);
			/* PS ready after 15 ms */
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_PS_RDY, 15);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_DATA_ALERT:
			data->alert_received = true;
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_GET_STATUS, 0);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	} else {
		/* Handle control message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_GET_SOURCE_CAP:
			tcpci_src_emul_send_capability_msg(data, common_data,
							   0);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_GET_REVISION:
			if (!common_data->rmdo) {
				tcpci_partner_send_control_msg(
					common_data, PD_CTRL_NOT_SUPPORTED, 0);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			tcpci_partner_send_data_msg(common_data,
						    PD_DATA_REVISION,
						    &common_data->rmdo, 1, 0);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	}
}

/**
 * @brief Handler for repeating SourceCapability message
 *
 * @param timer Pointer to timer which triggered timeout
 */
static void tcpci_src_emul_source_capability_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tcpci_src_emul_data *data = CONTAINER_OF(
		dwork, struct tcpci_src_emul_data, source_capability_timeout);
	struct tcpci_partner_data *common_data = data->common_data;

	if (k_mutex_lock(&common_data->transmit_mutex, K_NO_WAIT) != 0) {
		/*
		 * Emulator is probably handling received message,
		 * try later if timer wasn't stopped.
		 */
		k_work_submit(work);
		return;
	}

	/* Make sure that timer isn't stopped */
	if (k_work_busy_get(work) & K_WORK_CANCELING) {
		k_mutex_unlock(&common_data->transmit_mutex);
		return;
	}

	tcpci_src_emul_send_capability_msg_with_timer(data, common_data, 0);

	k_mutex_unlock(&common_data->transmit_mutex);
}

/**
 * @brief Perform action required by source device on hard or soft reset.
 *        Send source capabilities message and start SourceCapability timer.
 *
 * @param ext Pointer to USB-C source device emulator extension
 * @param common_data Pointer to USB-C device emulator common data
 */
static void tcpci_src_emul_reset(struct tcpci_partner_extension *ext,
				 struct tcpci_partner_data *common_data)
{
	struct tcpci_src_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_src_emul_data, ext);

	if (common_data->power_role != PD_ROLE_SOURCE) {
		return;
	}

	/* Send capability to establish PD again */
	tcpci_src_emul_send_capability_msg_with_timer(data, common_data, 1);
}

/**
 * @brief Ensure that there is correct role set after hard reset and perform
 *        source reset actions.
 *
 * @param ext Pointer to USB-C source device emulator extension
 * @param common_data Pointer to USB-C device emulator common data
 */
static void tcpci_src_emul_hard_reset(struct tcpci_partner_extension *ext,
				      struct tcpci_partner_data *common_data)
{
	if (common_data->power_role != PD_ROLE_SOURCE) {
		return;
	}

	tcpci_partner_common_hard_reset_as_role(common_data, PD_ROLE_SOURCE);
	tcpci_src_emul_reset(ext, common_data);
}

/**
 * @brief Disable source capabilities timer on disconnect
 *
 * @param ext Pointer to USB-C source device emulator extension
 * @param common_data Pointer to USB-C device emulator common data
 */
static void tcpci_src_emul_disconnect(struct tcpci_partner_extension *ext,
				      struct tcpci_partner_data *common_data)
{
	struct tcpci_src_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_src_emul_data, ext);

	tcpci_src_emul_stop_source_capability_timer(data);
}

/**
 * @brief Connect emulated device to TCPCI if common_data is configured as
 *        source
 *
 * @param ext Pointer to USB-C source device emulator extension
 * @param common_data Pointer to USB-C device emulator common data
 *
 * @return 0 on success
 * @return negative on TCPCI connect error
 */
static int
tcpci_src_emul_connect_to_tcpci(struct tcpci_partner_extension *ext,
				struct tcpci_partner_data *common_data)
{
	struct tcpci_src_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_src_emul_data, ext);

	if (common_data->power_role != PD_ROLE_SOURCE) {
		return 0;
	}

	common_data->cc1 = TYPEC_CC_VOLT_RP_3_0;
	common_data->cc2 = TYPEC_CC_VOLT_OPEN;
	common_data->polarity = POLARITY_CC1;

	/*
	 * It is not required to wait on connection before sending source
	 * capabilities, but it is permit. Timeout is obligatory for power swap.
	 */
	tcpci_src_emul_send_capability_msg_with_timer(
		data, common_data, TCPCI_SWAP_SOURCE_START_TIMEOUT_MS);

	return 0;
}

#define PDO_FIXED_FLAGS_MASK                                                  \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_UNCONSTRAINED | PDO_FIXED_COMM_CAP | \
	 PDO_FIXED_DATA_SWAP)

enum check_pdos_res tcpci_src_emul_check_pdos(struct tcpci_src_emul_data *data)
{
	int volt_i_min;
	int volt_i_max;
	int volt_min;
	int volt_max;
	int i;

	/* Check that first PDO is fixed 5V */
	if ((data->pdo[0] & PDO_TYPE_MASK) != PDO_TYPE_FIXED ||
	    PDO_FIXED_VOLTAGE(data->pdo[0]) != 5000) {
		return TCPCI_SRC_EMUL_FIRST_PDO_NO_FIXED_5V;
	}

	/* Check fixed PDOs are before other types and are in correct order */
	for (i = 1, volt_min = -1;
	     i < PDO_MAX_OBJECTS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_FIXED;
	     i++) {
		volt_i_min = PDO_FIXED_VOLTAGE(data->pdo[i]);
		/* Each voltage should be only once */
		if (volt_i_min == volt_min || volt_i_min == 5000) {
			return TCPCI_SRC_EMUL_FIXED_VOLT_REPEATED;
		}
		/* Check that voltage is increasing in next PDO */
		if (volt_i_min < volt_min) {
			return TCPCI_SRC_EMUL_FIXED_VOLT_NOT_IN_ORDER;
		}
		/* Check that fixed PDOs (except first) have cleared flags */
		if (data->pdo[i] & PDO_FIXED_FLAGS_MASK) {
			return TCPCI_SRC_EMUL_NON_FIRST_PDO_FIXED_FLAGS;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
	}

	/* Check battery PDOs are before variable type and are in order */
	for (volt_min = -1, volt_max = -1;
	     i < PDO_MAX_OBJECTS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_BATTERY;
	     i++) {
		volt_i_min = PDO_BATT_MIN_VOLTAGE(data->pdo[i]);
		volt_i_max = PDO_BATT_MAX_VOLTAGE(data->pdo[i]);
		/* Each voltage range should be only once */
		if (volt_i_min == volt_min && volt_i_max == volt_max) {
			return TCPCI_SRC_EMUL_BATT_VOLT_REPEATED;
		}
		/*
		 * Lower minimal voltage should be first, than lower maximal
		 * voltage.
		 */
		if (volt_i_min < volt_min ||
		    (volt_i_min == volt_min && volt_i_max < volt_max)) {
			return TCPCI_SRC_EMUL_BATT_VOLT_NOT_IN_ORDER;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
		volt_max = volt_i_max;
	}

	/* Check variable PDOs are last and are in correct order */
	for (volt_min = -1, volt_max = -1;
	     i < PDO_MAX_OBJECTS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_VARIABLE;
	     i++) {
		volt_i_min = PDO_VAR_MIN_VOLTAGE(data->pdo[i]);
		volt_i_max = PDO_VAR_MAX_VOLTAGE(data->pdo[i]);
		/* Each voltage range should be only once */
		if (volt_i_min == volt_min && volt_i_max == volt_max) {
			return TCPCI_SRC_EMUL_VAR_VOLT_REPEATED;
		}
		/*
		 * Lower minimal voltage should be first, than lower maximal
		 * voltage.
		 */
		if (volt_i_min < volt_min ||
		    (volt_i_min == volt_min && volt_i_max < volt_max)) {
			return TCPCI_SRC_EMUL_VAR_VOLT_NOT_IN_ORDER;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
		volt_max = volt_i_max;
	}

	/* Check that all PDOs after first 0 are unused and set to 0 */
	for (; i < PDO_MAX_OBJECTS; i++) {
		if (data->pdo[i] != 0) {
			return TCPCI_SRC_EMUL_PDO_AFTER_ZERO;
		}
	}

	return TCPCI_SRC_EMUL_CHECK_PDO_OK;
}

/** USB-C source device extension callbacks */
struct tcpci_partner_extension_ops tcpci_src_emul_ops = {
	.sop_msg_handler = tcpci_src_emul_handle_sop_msg,
	.hard_reset = tcpci_src_emul_hard_reset,
	.soft_reset = tcpci_src_emul_reset,
	.disconnect = tcpci_src_emul_disconnect,
	.connect = tcpci_src_emul_connect_to_tcpci,
};

struct tcpci_partner_extension *
tcpci_src_emul_init(struct tcpci_src_emul_data *data,
		    struct tcpci_partner_data *common_data,
		    struct tcpci_partner_extension *ext)
{
	struct tcpci_partner_extension *src_ext = &data->ext;

	/* By default there is only PDO 5v@3A */
	data->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);
	for (int i = 1; i < PDO_MAX_OBJECTS; i++) {
		data->pdo[i] = 0;
	}

	k_work_init_delayable(&data->source_capability_timeout,
			      tcpci_src_emul_source_capability_timeout);
	data->common_data = common_data;

	/* Use common handler to initialize roles */
	tcpci_partner_common_hard_reset_as_role(common_data, PD_ROLE_SOURCE);

	src_ext->next = ext;
	src_ext->ops = &tcpci_src_emul_ops;

	return src_ext;
}
