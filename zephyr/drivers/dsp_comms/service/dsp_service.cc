/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_dsp_service

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include <cstddef>

#include "ap_power/ap_power.h"
#include "cros/dsp/service/cros_transport.hh"
#include "cros/dsp/service/driver.hh"
#include "cros_board_info.h"
#include "hooks.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "proto/ec_dsp.pb.h"
#include "pw_assert/check.h"
#include "tablet_mode.h"

static_assert(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
              "Must have exactly 1 cros,dsp-service");

static constexpr const struct i2c_target_callbacks dsp_service_callbacks = {
    .write_requested = dsp_service_write_requested,
    .read_requested = dsp_service_read_requested,
    .write_received = dsp_service_write_received,
    .read_processed = dsp_service_read_processed,
#ifdef CONFIG_I2C_TARGET_BUFFER_MODE
    .buf_write_received = dsp_service_buf_write_received,
    .buf_read_requested = dsp_service_buf_read_requested,
#endif
    .stop = dsp_service_stop,
};

namespace cros::dsp::service {

Driver driver(DT_INST_REG_ADDR(0),
              &dsp_service_callbacks,
              DEVICE_DT_GET(DT_INST_BUS(0)),
              GPIO_DT_SPEC_INST_GET(0, int_gpios));

}  // namespace cros::dsp::service

static void dsp_service_startup(struct ap_power_ev_callback* cb,
                                struct ap_power_ev_data) {
  /* Only run this once */
  ap_power_ev_remove_callback(cb);

  cros::dsp::service::driver.Init().IgnoreError();
}

static int init_driver() {
  static struct ap_power_ev_callback cb;

  ap_power_ev_init_callback(&cb, dsp_service_startup, AP_POWER_STARTUP);
  ap_power_ev_add_callback(&cb);
  return 0;
}

SYS_INIT(init_driver, APPLICATION, 50);

LOG_MODULE_REGISTER(dsp_service, CONFIG_DSP_COMMS_LOG_LEVEL);

static inline int ParseGetCbiFlagsRequest(
    const cros_dsp_comms_GetCbiFlagsRequest& request,
    cros_dsp_comms_GetCbiFlagsResponse& response,
    enum cbi_data_tag& tag) {
  switch (request.which) {
    case cros_dsp_comms_CbiFlag_VERSION:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_BOARD_VERSION;
      LOG_DBG("Fetching BOARD_VERSION");
      break;
    case cros_dsp_comms_CbiFlag_OEM:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_OEM_ID;
      LOG_DBG("Fetching OEM_ID");
      break;
    case cros_dsp_comms_CbiFlag_SKU:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_SKU_ID;
      LOG_DBG("Fetching SKU_ID");
      break;
    case cros_dsp_comms_CbiFlag_MODEL:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_MODEL_ID;
      LOG_DBG("Fetching MODEL_ID");
      break;
    case cros_dsp_comms_CbiFlag_FW_CONFIG:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_FW_CONFIG;
      LOG_DBG("Fetching FW_CONFIG");
      break;
    case cros_dsp_comms_CbiFlag_PCB_SUPPLIER:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_PCB_SUPPLIER;
      LOG_DBG("Fetching PCB_SUPPLIER");
      break;
    case cros_dsp_comms_CbiFlag_SSFC:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_SSFC;
      LOG_DBG("Fetching SSFC");
      break;
    case cros_dsp_comms_CbiFlag_REWORK:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_64_tag;
      tag = CBI_TAG_REWORK_ID;
      LOG_DBG("Fetching REWORK_ID");
      break;
    case cros_dsp_comms_CbiFlag_FACTORY_CALIBRATION_DATA:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag;
      tag = CBI_TAG_FACTORY_CALIBRATION_DATA;
      LOG_DBG("Fetching FACTORY_CALIBRATION_DATA");
      break;
    case cros_dsp_comms_CbiFlag_DRAM_PART_NUM:
      response.which_flags =
          cros_dsp_comms_GetCbiFlagsResponse_flags_string_tag;
      tag = CBI_TAG_DRAM_PART_NUM;
      LOG_DBG("Fetching DRAM_PART_NUM");
      break;
    case cros_dsp_comms_CbiFlag_OEM_NAME:
      response.which_flags =
          cros_dsp_comms_GetCbiFlagsResponse_flags_string_tag;
      tag = CBI_TAG_OEM_NAME;
      LOG_DBG("Fetching OEM_NAME");
      break;
    default:
      LOG_WRN("Unsupported CBI read request");
      return -EINVAL;
  }
  return 0;
}

static inline int ReadCbiValue(cros_dsp_comms_GetCbiFlagsResponse& response,
                               enum cbi_data_tag tag) {
  uint8_t size;
  uint8_t max_size;
  int rc = -EINVAL;
  switch (response.which_flags) {
    case cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag:
      size = static_cast<uint8_t>(
          sizeof(cros_dsp_comms_GetCbiFlagsResponse::flags.flags_32));
      max_size = size;
      rc = cbi_get_board_info(
          tag, reinterpret_cast<uint8_t*>(&response.flags.flags_32), &size);
      break;
    case cros_dsp_comms_GetCbiFlagsResponse_flags_64_tag:
      size = static_cast<uint8_t>(
          sizeof(cros_dsp_comms_GetCbiFlagsResponse::flags.flags_64));
      max_size = size;
      rc = cbi_get_board_info(
          tag, reinterpret_cast<uint8_t*>(&response.flags.flags_64), &size);
      break;
    case cros_dsp_comms_GetCbiFlagsResponse_flags_string_tag:
      size = static_cast<uint8_t>(
          sizeof(cros_dsp_comms_GetCbiFlagsResponse::flags.flags_string));
      max_size = size;
      rc = cbi_get_board_info(
          tag, reinterpret_cast<uint8_t*>(response.flags.flags_string), &size);
      break;
    default:
      break;
  }
  if (rc == 0 && size > max_size) {
    rc = -EOVERFLOW;
  }
  return rc;
}

void dsp_service_handle_get_cbi_flags_request(struct k_work*) {
  const cros_dsp_comms_GetCbiFlagsRequest* request =
      &(cros::dsp::service::driver.pending_service_request_.request
            .get_cbi_flags);
  // Initialize to anything to avoid -Werror=maybe-uninitialized
  enum cbi_data_tag tag = CBI_TAG_BOARD_VERSION;
  int rc = 0;
  cros_dsp_comms_GetCbiFlagsResponse response =
      cros_dsp_comms_GetCbiFlagsResponse_init_zero;

  LOG_DBG("GOT: GetCbiFlagsRequest, which=%d", request->which);
  rc = ParseGetCbiFlagsRequest(*request, response, tag);

  if (rc == 0) {
    rc = ReadCbiValue(response, tag);
  }

  if (rc == 0) {
    // Attempt to encode
    if (!cros::dsp::service::driver.transport_.StageResponse(response).ok()) {
      LOG_ERR("Failed to stage response");
      rc = -EINVAL;
    }
  }

  if (rc != 0) {
    LOG_ERR("Failed to read CBI value");
    cros::dsp::service::driver.transport_.SetStatusBit(
        cros_dsp_comms_StatusFlag_STATUS_FLAG_PROCESSING_ERROR, true);
  }
}

bool cros::dsp::service::Driver::AttemptToDecode() {
  // Try to decode
  pb_istream_t istream =
      pb_istream_from_buffer(request_buffer_, request_buffer_size_);
  return pb_decode(
      &istream, cros_dsp_comms_EcService_fields, &pending_service_request_);
}

void cros::dsp::service::Driver::SetNotebookMode(
    cros_dsp_comms_NotebookMode mode) {
  // Bail if remote lid angle is not enabled or if the lid isn't open.
  if (!IS_ENABLED(CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE) || !lid_is_open()) {
    return;
  }
  switch (mode) {
    case cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_NOTEBOOK:
      LOG_DBG("    NOTEBOOK mode, tablet_get_mode()=%d", tablet_get_mode());
      tablet_set_mode(0, TABLET_TRIGGER_LID);
      break;
    case cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_TABLET:
      LOG_DBG("    TABLET mode, tablet_get_mode()=%d", tablet_get_mode());
      tablet_set_mode(1, TABLET_TRIGGER_LID);
      break;
    default:
      LOG_WRN("Unsupported notebook mode");
      break;
  }
}

bool cros::dsp::service::Driver::HandleDecodedRequest() {
  switch (pending_service_request_.which_request) {
    case cros_dsp_comms_EcService_notify_notebook_mode_change_tag:
      LOG_DBG("GOT: NotifyNotebookModeChangeRequest");
      SetNotebookMode(pending_service_request_.request
                          .notify_notebook_mode_change.new_mode);
      return false;
    case cros_dsp_comms_EcService_get_cbi_flags_tag:
      LOG_DBG("Scheduling get_cbi_flags_work");
      k_work_submit(&get_cbi_flags_work_);
      return true;
    case cros_dsp_comms_EcService_reset_connection_tag:
      LOG_DBG("Resetting connection");
      // The connection is reset so flush all the pending messages from the old
      // session.
      while (transport_.ReadNextMessage().status().ok()) {
      }
      return false;
    default:
      LOG_WRN("Unsupported request type");
      cros::dsp::service::driver.transport_.SetStatusBit(
          cros_dsp_comms_StatusFlag_STATUS_FLAG_PROCESSING_ERROR, true);
      return false;
  }
}

pw::Status cros::dsp::service::Driver::Init() {
  int rc;
  k_work_init(&get_cbi_flags_work_, dsp_service_handle_get_cbi_flags_request);

  rc = k_sem_init(&data_processing_semaphore_, 1, 1);
  PW_CHECK_INT_EQ(rc, 0);

  LOG_INF("Setting up target %s::0x%02x", bus_->name, target_cfg_.address);

  rc |= i2c_target_register(bus_, &target_cfg_);
  PW_CHECK_INT_EQ(rc, 0);

  PW_CHECK(gpio_is_ready_dt(&interrupt_));

  rc |= gpio_pin_configure_dt(&interrupt_, GPIO_OUTPUT);
  PW_CHECK_INT_EQ(rc, 0);

  rc |= gpio_pin_set_dt(&interrupt_, CROS_DSP_GPIO_OFF);
  PW_CHECK_INT_EQ(rc, 0);

  transport_.SetNotifyClientCallback([this](bool has_data) {
    LOG_DBG("NotifyClientCallback(%d)", has_data);
    if (has_data) {
      k_sem_give(&this->data_processing_semaphore_);
      int rc = gpio_pin_set_dt(&this->interrupt_, CROS_DSP_GPIO_ON);
      LOG_DBG("asserting GPIO (%d)", rc);
    } else {
      int rc = gpio_pin_set_dt(&this->interrupt_, CROS_DSP_GPIO_OFF);
      LOG_DBG("deasserting GPIO (%d)", rc);
    }
  });

  LOG_INF("DSP Initialization rc=%d", rc);

  return pw::OkStatus();
}

void dsp_service_hook_lid_change() {
  bool is_lid_open = lid_is_open() != 0;
  LOG_DBG("is_lid_open=%d", is_lid_open);
  cros::dsp::service::driver.transport_.SetStatusBit(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_LID_OPEN, is_lid_open);
}
DECLARE_HOOK(HOOK_LID_CHANGE, dsp_service_hook_lid_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, dsp_service_hook_lid_change, HOOK_PRIO_DEFAULT);

void dsp_service_hook_tablet_mode_change() {
  bool is_in_tablet_mode = tablet_get_mode() != 0;
  LOG_DBG("is_in_tablet_mode=%d", is_in_tablet_mode);
  cros::dsp::service::driver.transport_.SetStatusBit(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_TABLET_MODE, is_in_tablet_mode);
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE,
             dsp_service_hook_tablet_mode_change,
             HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, dsp_service_hook_tablet_mode_change, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_TEST
/*
 * There's a bug in the i2c_emul where every node in the i2c is required to have
 * a device* associated with it. So create a stub one for now until upstream is
 * patched up.
 */
int dsp_service_init(const struct device* dev) {
  ARG_UNUSED(dev);
  return 0;
}

DEVICE_DT_INST_DEFINE(
    0, dsp_service_init, NULL, NULL, NULL, POST_KERNEL, 99, NULL);
#endif