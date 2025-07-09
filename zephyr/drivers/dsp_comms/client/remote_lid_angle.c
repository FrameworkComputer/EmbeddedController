/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pb_encode.h>
#include <zephyr/logging/log.h>

#include "cros/dsp/client.h"
#include "hooks.h"
#include "proto/ec_dsp.pb.h"
#include "tablet_mode.h"

LOG_MODULE_DECLARE(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

static void lid_angle_remote_peripheral_enable() {
  bool is_tablet_mode = tablet_get_mode();
  const struct dsp_client_config* cfg = default_client_device->config;
  struct dsp_client_data* data = default_client_device->data;
  const cros_dsp_comms_NotebookMode new_mode =
      (is_tablet_mode ? cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_TABLET
                      : cros_dsp_comms_NotebookMode_NOTEBOOK_MODE_NOTEBOOK);
  cros_dsp_comms_EcService service = {
      .which_request = cros_dsp_comms_EcService_notify_notebook_mode_change_tag,
      .request =
          {
              .notify_notebook_mode_change =
                  {
                      .new_mode = new_mode,
                  },
          },
  };

  LOG_INF("Waiting for lock to send tablet_mode(%d)", is_tablet_mode);
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

DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE,
             lid_angle_remote_peripheral_enable,
             HOOK_PRIO_DEFAULT);
