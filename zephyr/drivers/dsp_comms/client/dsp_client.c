/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

#define DT_DRV_COMPAT cros_dsp_client

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include "cros/dsp/client.h"
#include "proto/ec_dsp.pb.h"
#include "pw_transport/proto/transport.pb.h"
#include "tablet_mode.h"

LOG_MODULE_REGISTER(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

static void dsp_client_read_status(struct k_work* item);

const struct device* default_client_device =
    DEVICE_DT_GET(DT_INST(0, DT_DRV_COMPAT));

int cbi_remote_get_board_info(enum cbi_data_tag tag,
                              uint8_t* buffer,
                              uint8_t* buffer_size) {
  int rc;
  cros_dsp_comms_CbiFlag flag;
  cros_dsp_comms_GetCbiFlagsResponse response =
      cros_dsp_comms_GetCbiFlagsResponse_init_default;

  switch (tag) {
    case CBI_TAG_BOARD_VERSION:
      LOG_DBG("Getting BOARD_VERSION");
      flag = cros_dsp_comms_CbiFlag_VERSION;
      break;
    case CBI_TAG_OEM_ID:
      LOG_DBG("Getting OEM_ID");
      flag = cros_dsp_comms_CbiFlag_OEM;
      break;
    case CBI_TAG_SKU_ID:
      LOG_DBG("Getting SKU_ID");
      flag = cros_dsp_comms_CbiFlag_SKU;
      break;
    case CBI_TAG_MODEL_ID:
      LOG_DBG("Getting MODEL_ID");
      flag = cros_dsp_comms_CbiFlag_MODEL;
      break;
    case CBI_TAG_FW_CONFIG:
      LOG_DBG("Getting FW_CONFIG");
      flag = cros_dsp_comms_CbiFlag_FW_CONFIG;
      break;
    case CBI_TAG_PCB_SUPPLIER:
      LOG_DBG("Getting PCB_SUPPLIER");
      flag = cros_dsp_comms_CbiFlag_PCB_SUPPLIER;
      break;
    case CBI_TAG_SSFC:
      LOG_DBG("Getting SSFC");
      flag = cros_dsp_comms_CbiFlag_SSFC;
      break;
    case CBI_TAG_REWORK_ID:
      LOG_DBG("Getting REWORK_ID");
      flag = cros_dsp_comms_CbiFlag_REWORK;
      break;
    case CBI_TAG_FACTORY_CALIBRATION_DATA:
      LOG_DBG("Getting FACTORY_CALIBRATION_DATA");
      flag = cros_dsp_comms_CbiFlag_FACTORY_CALIBRATION_DATA;
      break;
    case CBI_TAG_DRAM_PART_NUM:
      LOG_DBG("Getting DRAM_PART_NUM");
      flag = cros_dsp_comms_CbiFlag_DRAM_PART_NUM;
      break;
    case CBI_TAG_OEM_NAME:
      LOG_DBG("Getting OEM_NAME");
      flag = cros_dsp_comms_CbiFlag_OEM_NAME;
      break;
    default:
      LOG_ERR("TAG not supported");
      return -EINVAL;
  }

  rc = dsp_client_get_cbi_flags(default_client_device, flag, &response);

  if (rc != 0) {
    LOG_ERR("Failed to get CBI flags");
    return rc;
  }

  memset(buffer, 0, *buffer_size);
  switch (response.which_flags) {
    case cros_dsp_comms_GetCbiFlagsResponse_flags_32_tag:
      if (*buffer_size < 4) {
        LOG_ERR("Not enough memory");
        return -ENOMEM;
      }
      memcpy(buffer, &response.flags.flags_32, 4);
      *buffer_size = 4;
      break;
    case cros_dsp_comms_GetCbiFlagsResponse_flags_64_tag:
      if (*buffer_size < 8) {
        LOG_ERR("Not enough memory");
        return -ENOMEM;
      }
      memcpy(buffer, &response.flags.flags_64, 8);
      *buffer_size = 8;
      break;
    case cros_dsp_comms_GetCbiFlagsResponse_flags_string_tag:
      if (*buffer_size < strlen(response.flags.flags_string)) {
        LOG_ERR("Not enough memory");
        return -ENOMEM;
      }
      memcpy(buffer,
             &response.flags.flags_string,
             strlen(response.flags.flags_string));
      *buffer_size = strlen(response.flags.flags_string);
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

#define PENDING_RESPONSE_LENGTH_ERROR UINT32_MAX

#define GET_STATUS_RESPONSE_FLAG_SIZE \
  ARRAY_SIZE(((pw_transport_Status*)0)->flags_mask)

static inline bool is_status_bit_set(uint8_t bit,
                                     const pw_transport_Status* status) {
  __ASSERT_NO_MSG(bit < GET_STATUS_RESPONSE_FLAG_SIZE * 8);
  return ((status->flags_mask[bit / 8] >> (bit % 8)) & 0x1) == 1;
}

// TODO add dedicated function to reading the status

static int dsp_client_enable_interrupt(struct dsp_client_data* data,
                                       bool enable) {
  const struct dsp_client_config* config = data->config;

  if (data->interrupt_config == GPIO_INT_EDGE_TO_ACTIVE) {
    // We're using an edge trigger, there's no need to enable/disable the
    // interrupt
    return 0;
  }

  // This function only does anything for level interrupts to avoid getting
  // repeated interrupts while we're servicing the first
  if (enable) {
    LOG_INF("Enabling level interrupts!");
    return gpio_pin_interrupt_configure_dt(&config->interrupt,
                                           GPIO_INT_LEVEL_ACTIVE);
  } else {
    LOG_INF("Disabling interrupts!");
    return gpio_pin_interrupt_configure_dt(&config->interrupt,
                                           GPIO_INT_DISABLE);
  }
}

int dsp_client_get_cbi_flags(const struct device* dev,
                             cros_dsp_comms_CbiFlag flag,
                             cros_dsp_comms_GetCbiFlagsResponse* mem) {
  const struct dsp_client_config* cfg = dev->config;
  struct dsp_client_data* data = dev->data;
  cros_dsp_comms_EcService service = {
      .which_request = cros_dsp_comms_EcService_get_cbi_flags_tag,
      .request =
          {
              .get_cbi_flags =
                  {
                      .which = flag,
                  },
          },
  };
  int rc;

  __ASSERT(device_is_ready(dev), "DSP client not ready!");
  k_mutex_lock(&data->mutex, K_FOREVER);
  pb_ostream_t stream = pb_ostream_from_buffer(data->request_buffer,
                                               cros_dsp_comms_EcService_size);
  bool encode_status =
      pb_encode(&stream, cros_dsp_comms_EcService_fields, &service);

  ARG_UNUSED(encode_status);
  __ASSERT_NO_MSG(encode_status);

  /* Write the message */
  LOG_DBG("Writing %zu bytes", stream.bytes_written);
  rc = i2c_write_dt(&cfg->i2c, data->request_buffer, stream.bytes_written);

  if (rc != 0) {
    k_mutex_unlock(&data->mutex);
    return rc;
  }

  /* Wait for the EC to process the request */
  LOG_DBG("Waiting...");
  uint32_t events =
      k_event_wait(&data->response_ready_event,
                   UINT32_MAX,
                   true,
                   K_MSEC(CONFIG_PLATFORM_EC_DSP_CLIENT_TIMEOUT_MS));

  if (events == 0) {
    LOG_ERR("Timed out waiting for response");
    k_mutex_unlock(&data->mutex);
    dsp_client_enable_interrupt(data, true);
    return -EAGAIN;
  }
  LOG_DBG("events = 0x%08x", events);

  LOG_DBG("Expecting response of %u bytes", data->pending_response_length);
  if (data->pending_response_length == PENDING_RESPONSE_LENGTH_ERROR) {
    LOG_ERR("Remote failed to get value");
    k_mutex_unlock(&data->mutex);
    return -EINVAL;
  }

  rc = i2c_read_dt(
      &cfg->i2c, data->response_buffer, data->pending_response_length);
  if (rc != 0) {
    k_mutex_unlock(&data->mutex);
    return rc;
  }

  pb_istream_t istream = pb_istream_from_buffer(data->response_buffer,
                                                data->pending_response_length);
  bool decode_status =
      pb_decode(&istream, cros_dsp_comms_GetCbiFlagsResponse_fields, mem);

  if (!decode_status) {
    LOG_ERR("Failed to decode response");
    k_mutex_unlock(&data->mutex);
    return -EINTR;
  }

  k_mutex_unlock(&data->mutex);
  return rc;
}

static void dsp_client_gpio_callback(const struct device* port,
                                     struct gpio_callback* cb,
                                     uint32_t pin) {
  struct dsp_client_data* data =
      CONTAINER_OF(cb, struct dsp_client_data, gpio_cb);
  ARG_UNUSED(port);
  ARG_UNUSED(pin);

  LOG_DBG("***** DSP SERVICE FIRED INTERRUPT *****");
  dsp_client_enable_interrupt(data, false);
  k_work_submit(&data->read_status_work);
}

static void dsp_client_read_status(struct k_work* item) {
  struct dsp_client_data* data =
      CONTAINER_OF(item, struct dsp_client_data, read_status_work);
  const struct dsp_client_config* cfg = data->config;
  int rc;

  ARG_UNUSED(item);

  /* Read the status */
  uint8_t status_buffer[pw_transport_Status_size + 4] = {0};
  pw_transport_Status status;
  LOG_DBG("Reading Status bytes");
  rc = i2c_read_dt(&cfg->i2c, status_buffer, ARRAY_SIZE(status_buffer));
  dsp_client_enable_interrupt(data, true);
  if (rc != 0) {
    k_mutex_unlock(&data->mutex);
    return;
  }

  pb_istream_t istream =
      pb_istream_from_buffer(status_buffer, ARRAY_SIZE(status_buffer));
  bool decode_status =
      pb_decode_delimited(&istream, pw_transport_Status_fields, &status);

  if (!decode_status) {
    LOG_ERR("Failed to decode Status");
    return;
  }
  bool is_response_ready = is_status_bit_set(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_RESPONSE_READY, &status);
  bool is_processing_error = is_status_bit_set(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_PROCESSING_ERROR, &status);
  bool is_lid_open = is_status_bit_set(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_LID_OPEN, &status);
  bool is_360 = is_status_bit_set(
      cros_dsp_comms_StatusFlag_STATUS_FLAG_TABLET_MODE, &status);
  LOG_DBG("response_ready? %d, response_length=%u",
          is_response_ready,
          status.response_length);
  LOG_DBG("processing error? %d", is_processing_error);
  if (is_response_ready) {
    data->pending_response_length = status.response_length;
    k_event_post(&data->response_ready_event, 1);
  } else if (is_processing_error) {
    data->pending_response_length = PENDING_RESPONSE_LENGTH_ERROR;
    k_event_post(&data->response_ready_event, 2);
  }

  LOG_DBG("is_lid_open=%d, is_360=%d", is_lid_open, is_360);
  if (IS_ENABLED(CONFIG_PLATFORM_EC_DSP_REMOTE_LID_SWITCH)) {
    remote_lid_switch_set(is_lid_open);
  }
  if (IS_ENABLED(CONFIG_PLATFORM_EC_DSP_REMOTE_TABLET_SWITCH)) {
    remote_tablet_switch_set(is_360);
  }
}

static int dsp_client_gpio_init(const struct device* dev) {
  const struct dsp_client_config* config = dev->config;
  struct dsp_client_data* data = dev->data;
  int rc = 0;
  int interrupt_rc;

  __ASSERT_NO_MSG(gpio_is_ready_dt(&config->interrupt));
  LOG_INF("Initializing %s::%u with flags 0x%04x",
          config->interrupt.port->name,
          config->interrupt.pin,
          config->interrupt.dt_flags);

  rc = gpio_pin_configure_dt(&config->interrupt, GPIO_INPUT);
  __ASSERT_NO_MSG(rc == 0);

  gpio_init_callback(
      &data->gpio_cb, dsp_client_gpio_callback, BIT(config->interrupt.pin));
  rc |= gpio_add_callback(config->interrupt.port, &data->gpio_cb);
  __ASSERT_NO_MSG(rc == 0);

  // This driver prefers level active interrupts since the server might have an
  // interrupt running before the client even come up. But if we can't get a
  // level interrupt we'll fall back to edge.
  interrupt_rc = gpio_pin_interrupt_configure_dt(&config->interrupt,
                                                 GPIO_INT_LEVEL_ACTIVE);
  if (interrupt_rc == 0) {
    data->interrupt_config = GPIO_INT_LEVEL_ACTIVE;
  } else {
    interrupt_rc = gpio_pin_interrupt_configure_dt(&config->interrupt,
                                                   GPIO_INT_EDGE_TO_ACTIVE);
    if (interrupt_rc == 0) {
      data->interrupt_config = GPIO_INT_EDGE_TO_ACTIVE;
    }
  }
  rc |= interrupt_rc;
  __ASSERT_NO_MSG(rc == 0);

  if (data->interrupt_config == GPIO_INT_LEVEL_ACTIVE) {
    LOG_INF("Interrupt configured to LEVEL_ACTIVE");
  } else if (data->interrupt_config == GPIO_INT_EDGE_TO_ACTIVE) {
    LOG_INF("Interrupt configured to EDGE_TO_ACTIVE");
  } else {
    LOG_INF("Interrupt configured to %d", data->interrupt_config);
  }

  return rc;
}

static int dsp_client_init(const struct device* dev) {
  struct dsp_client_data* data = dev->data;
  const struct dsp_client_config* config = data->config;
  int rc;

  __ASSERT(device_is_ready(config->i2c.bus), "DSP client not ready!");

  k_mutex_init(&data->mutex);
  k_event_init(&data->response_ready_event);
  k_work_init(&data->read_status_work, dsp_client_read_status);

  rc = dsp_client_gpio_init(dev);
  if (rc != 0) {
    return rc;
  }

  cros_dsp_comms_EcService service = {
      .which_request = cros_dsp_comms_EcService_reset_connection_tag,
      .request = {},
  };
  pb_ostream_t stream = pb_ostream_from_buffer(data->request_buffer,
                                               cros_dsp_comms_EcService_size);
  bool encode_status =
      pb_encode(&stream, cros_dsp_comms_EcService_fields, &service);

  ARG_UNUSED(encode_status);
  __ASSERT_NO_MSG(encode_status);
  rc = i2c_write_dt(&config->i2c, data->request_buffer, stream.bytes_written);

  if (rc != 0) {
    LOG_ERR("Failed to write reset message");
  }

  return rc;
}

#define DSP_CLIENT_DEFINE(inst)                                \
  static struct dsp_client_config dsp_client_config_##inst = { \
      .i2c = I2C_DT_SPEC_INST_GET(inst),                       \
      .interrupt = GPIO_DT_SPEC_INST_GET(inst, int_gpios),     \
  };                                                           \
  static struct dsp_client_data dsp_client_data_##inst = {     \
      .config = &dsp_client_config_##inst,                     \
  };                                                           \
  DEVICE_DT_INST_DEFINE(inst,                                  \
                        dsp_client_init,                       \
                        NULL,                                  \
                        &dsp_client_data_##inst,               \
                        &dsp_client_config_##inst,             \
                        POST_KERNEL,                           \
                        CONFIG_PLATFORM_EC_DSP_INIT_PRIORITY,  \
                        NULL);

DT_INST_FOREACH_STATUS_OKAY(DSP_CLIENT_DEFINE)
