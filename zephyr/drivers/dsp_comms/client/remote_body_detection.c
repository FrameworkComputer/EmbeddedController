/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pb_encode.h>
#include <zephyr/logging/log.h>

#include "body_detection_client.h"
#include "cros/dsp/client.h"
#include "hooks.h"
#include "proto/ec_dsp.pb.h"

LOG_MODULE_DECLARE(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

static void body_detection_remote_change() {
  const struct dsp_client_config* cfg = default_client_device->config;
  struct dsp_client_data* data = default_client_device->data;
  enum body_detect_states state = body_detect_get_state();
  cros_dsp_comms_EcService service = {
      .which_request =
          cros_dsp_comms_EcService_notify_body_detection_change_tag,
      .request =
          {
              .notify_body_detection_change =
                  {
                      .on_body = (state == BODY_DETECTION_ON_BODY),
                  },
          },
  };

  LOG_INF("Waiting for lock to send on-body event");
  k_mutex_lock(&data->mutex, K_FOREVER);
  pb_ostream_t stream = pb_ostream_from_buffer(data->request_buffer,
                                               cros_dsp_comms_EcService_size);
  bool encode_status =
      pb_encode(&stream, cros_dsp_comms_EcService_fields, &service);

  if (!encode_status) {
    LOG_ERR("Failed to encode request");
    k_mutex_unlock(&data->mutex);
    return;
  }

  /* Write the message */
  LOG_DBG("Writing %zu bytes", stream.bytes_written);
  int rc = i2c_write_dt(&cfg->i2c, data->request_buffer, stream.bytes_written);
  if (rc != 0) {
    LOG_ERR("Failed to send request (%d)", rc);
  }
  k_mutex_unlock(&data->mutex);
}

DECLARE_HOOK(HOOK_BODY_DETECT_CHANGE,
             body_detection_remote_change,
             HOOK_PRIO_DEFAULT);
