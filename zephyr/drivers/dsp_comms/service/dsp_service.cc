/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include <cstddef>

#include "ap_power/ap_power.h"
#include "body_detection_client.h"
#include "cros/dsp/service/cros_transport.hh"
#include "cros/dsp/service/driver.hh"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "proto/ec_dsp.pb.h"
#include "pw_assert/check.h"
#include "tablet_mode.h"

#define DT_DRV_COMPAT cros_dsp_service

LOG_MODULE_REGISTER(dsp_service, CONFIG_DSP_COMMS_LOG_LEVEL);

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
    .error = dsp_service_error,
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
    case cros_dsp_comms_CbiFlag_UFSC:
      response.which_flags = cros_dsp_comms_GetCbiFlagsResponse_ufsc_tag;
      tag = CBI_TAG_UFSC;
      LOG_DBG("Fetching UFSC");
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
    case cros_dsp_comms_GetCbiFlagsResponse_ufsc_tag:
      size = static_cast<uint8_t>(
          sizeof(cros_dsp_comms_GetCbiFlagsResponse::flags.ufsc.data));
      max_size = size;
      rc = cbi_get_board_info(
          tag, reinterpret_cast<uint8_t*>(response.flags.ufsc.data), &size);
      break;
    default:
      break;
  }
  if (rc == 0 && size > max_size) {
    rc = -EOVERFLOW;
  }
  return rc;
}

static void mode_handling_delayed(struct k_work* work) {
  ARG_UNUSED(work);
  int mode_val = cros::dsp::service::driver.get_mode_val();
  if (IS_ENABLED(CONFIG_PLATFORM_EC_TABLET_MODE)) {
    tablet_set_mode(mode_val, TABLET_TRIGGER_LID);
  }
  if (IS_ENABLED(CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE)) {
    lid_angle_peripheral_enable(!mode_val);
  }
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
  // Bail if tablet mode is not enabled or if the lid isn't open.
  if (!IS_ENABLED(CONFIG_PLATFORM_EC_TABLET_MODE) || !lid_is_open()) {
    return;
  }
  switch (mode) {
    case cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_NOTEBOOK:
      LOG_DBG("    NOTEBOOK mode, tablet_get_mode()=%d", tablet_get_mode());
      cros::dsp::service::driver.mode_val = 0;
      break;
    case cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_TABLET:
      LOG_DBG("    TABLET mode, tablet_get_mode()=%d", tablet_get_mode());
      cros::dsp::service::driver.mode_val = 1;
      break;
    default:
      LOG_WRN("Unsupported notebook mode");
      return;
  }
  k_work_reschedule(&mode_handling_work_,
                    K_MSEC(DSP_SERVICE_MODE_HANDLE_DELAY_MS));
}

bool cros::dsp::service::Driver::HandleDecodedRequest() {
  switch (pending_service_request_.which_request) {
    case cros_dsp_comms_EcService_notify_notebook_mode_change_tag:
      LOG_DBG("GOT: NotifyNotebookModeChangeRequest");
      SetNotebookMode(pending_service_request_.request
                          .notify_notebook_mode_change.new_mode);
      return false;
#ifdef CONFIG_PLATFORM_EC_DSP_REMOTE_BODY_DETECTION
    case cros_dsp_comms_EcService_notify_body_detection_change_tag:
      LOG_DBG("GOT: NotifyBodyDetectionChangeRequest");
      body_detect_change_state_extern(
          pending_service_request_.request.notify_body_detection_change.on_body
              ? BODY_DETECTION_ON_BODY
              : BODY_DETECTION_OFF_BODY);
      return false;
#endif
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
      // Following the service reset, call lid_change and table_mode_change
      // hooks to ensure that the status message has the correct current status
      // if the ISH requests the current status.
      dsp_service_hook_lid_change();
      if (IS_ENABLED(CONFIG_PLATFORM_EC_TABLET_MODE)) {
        dsp_service_hook_tablet_mode_change();
      }
      return false;
    default:
      LOG_WRN("Unsupported request type");
      cros::dsp::service::driver.transport_.SetStatusBit(
          cros_dsp_comms_StatusFlag_STATUS_FLAG_PROCESSING_ERROR, true);
      return false;
  }
}

cros::dsp::service::Driver::Driver(
    uint16_t target_address,
    const struct i2c_target_callbacks* target_callbacks,
    const struct device* bus,
    struct gpio_dt_spec interrupt)
    : target_cfg_{},
      bus_(bus),
      interrupt_(interrupt),
      transport_([this](bool has_data) {
        LOG_DBG("NotifyClientCallback(%d)", has_data);
        if (has_data) {
          k_sem_give(&this->data_processing_semaphore_);
          int rc = gpio_pin_set_dt(&this->interrupt_, CROS_DSP_GPIO_ON);
          LOG_DBG("asserting GPIO (%d)", rc);
        } else {
          int rc = gpio_pin_set_dt(&this->interrupt_, CROS_DSP_GPIO_OFF);
          LOG_DBG("deasserting GPIO (%d)", rc);
        }
      }) {
  int rc = k_sem_init(&data_processing_semaphore_, 1, 1);
  PW_CHECK_INT_EQ(rc, 0);

  target_cfg_.address = target_address;
  target_cfg_.callbacks = target_callbacks;
}

pw::Status cros::dsp::service::Driver::Init() {
  int rc = 0;
#if DT_PROP(DT_DRV_INST(0), allow_runtime_disable)
  // Check if the FW config is set to the disable value
  uint32_t ish_enabled;

  rc |= cros_cbi_get_fw_config(ISH, &ish_enabled);
  PW_CHECK_INT_EQ(rc, 0);

  if (ish_enabled == ISH_DISABLED) {
    // Match, disable the service
    LOG_INF("Disabling DSP comms service");
    return pw::OkStatus();
  }
#endif
  k_work_init(&get_cbi_flags_work_, dsp_service_handle_get_cbi_flags_request);
  k_work_init_delayable(&mode_handling_work_, mode_handling_delayed);

  LOG_INF("Setting up target %s::0x%02x", bus_->name, target_cfg_.address);

  rc |= i2c_target_register(bus_, &target_cfg_);
  PW_CHECK_INT_EQ(rc, 0);

  PW_CHECK(gpio_is_ready_dt(&interrupt_));

  rc |= gpio_pin_configure_dt(&interrupt_, GPIO_OUTPUT);
  PW_CHECK_INT_EQ(rc, 0);

  rc |= gpio_pin_set_dt(&interrupt_, CROS_DSP_GPIO_OFF);
  PW_CHECK_INT_EQ(rc, 0);

  LOG_INF("DSP Initialization rc=%d", rc);
  if (rc == 0) {
    // Start off in notebook mode until DSP has a chance to calculate lid angle
    if (IS_ENABLED(CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE)) {
      SetNotebookMode(cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_NOTEBOOK);
    }
    /* Poll the GMR states */
    dsp_service_hook_lid_change();
    if (IS_ENABLED(CONFIG_PLATFORM_EC_TABLET_MODE)) {
      dsp_service_hook_tablet_mode_change();
    }
  }

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

#ifdef CONFIG_PLATFORM_EC_TABLET_MODE
void dsp_service_hook_tablet_mode_change() {
  bool is_in_tablet_mode = !gpio_get_level(GPIO_TABLET_MODE_L);
  LOG_DBG("is_in_tablet_mode=%d", is_in_tablet_mode);
  cros::dsp::service::driver.transport_.SetStatusBit(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_TABLET_MODE, is_in_tablet_mode);
}
DECLARE_HOOK(HOOK_INIT, dsp_service_hook_tablet_mode_change, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_PLATFORM_EC_TABLET_MODE */

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
