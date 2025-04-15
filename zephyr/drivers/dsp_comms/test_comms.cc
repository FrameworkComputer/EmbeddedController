/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>

#include <cstring>
#include <limits>

#include "ap_power/ap_power_events.h"
#include "cros/dsp/client.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "proto/ec_dsp.pb.h"
#include "pw_assert/check.h"
#include "pw_transport/proto/transport.pb.h"
#include "tablet_mode.h"

// DECLARE_FAKE_VALUE_FUNC(int, crec_flash_unprotected_read, int, int, char *);

namespace {
constexpr const struct device* kClient =
    DEVICE_DT_GET_OR_NULL(DT_ALIAS(test_dsp_client));
static_assert(kClient != nullptr, "Missing alias 'test_dsp_client'");

constexpr const struct i2c_dt_spec kClientBusSpec =
    I2C_DT_SPEC_GET(DT_ALIAS(test_dsp_client));

constexpr const struct gpio_dt_spec kLidOpenInterruptSpec = {
    .port =
        DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(DT_ALIAS(test_lid_open), gpios, 0)),
    .pin = DT_GPIO_PIN_BY_IDX(DT_ALIAS(test_lid_open), gpios, 0),
    .dt_flags = 0,
};

constexpr const struct gpio_dt_spec kClientInterruptSpec =
    GPIO_DT_SPEC_GET(DT_ALIAS(test_dsp_client), int_gpios);
constexpr const struct gpio_dt_spec kServiceInterruptSpec =
    GPIO_DT_SPEC_GET(DT_ALIAS(test_dsp_service), int_gpios);

constexpr const uint32_t kDefaultBoardVersion = 0x12345678;
constexpr const uint32_t kDefaultOemId = 0x23456789;
constexpr const uint32_t kDefaultSkuId = 0x3456789a;
constexpr const uint32_t kDefaultModelId = 0x456789ab;
constexpr const uint32_t kDefaultFwConfig = 0x56789abc;
constexpr const uint32_t kDefaultPcbSupplier = 0x6789abcd;
constexpr const uint32_t kDefaultSsfc = 0x789abcde;
constexpr const uint64_t kDefaultReworkId = 0x89abcdef01234567;
constexpr const uint32_t kDefaultFactoryCalibrationData = 0x9abcdef0;
constexpr const char* kDefaultDramPartNum = "DRAM-123";
constexpr const char* kDefaultOemName = "Google";

#define SUSPEND() k_usleep(1)

inline bool IsFlagSet(const pw_transport_Status& status, uint64_t bit) {
  return ((status.flags_mask[bit / 8] >> (bit % 8)) & 0x1) == 1;
}

class DspComms : public ::testing::Test {
 private:
  static void OneTimeInit() {
    static bool is_initialized = false;
    if (is_initialized) {
      return;
    }
    ap_power_ev_send_callbacks(AP_POWER_STARTUP);
    PW_ASSERT(0 == device_init(kClient));
    // We have to poke any CBI value to make sure that it was
    // initialized
    uint32_t ver;
    cbi_get_board_version(&ver);
    is_initialized = true;
  }

 protected:
  DspComms() = default;

  void SetUp() override {
    // RESET_FAKE(crec_flash_unprotected_read);

    // crec_flash_unprotected_read_fake.custom_fake =
    // crec_flash_physical_read; FFF_RESET_HISTORY(); Set default
    // values

    OneTimeInit();
    cbi_create();

    ClearTransport();

    SUSPEND();
    ASSERT_EQ(0,
              cbi_set_board_info(
                  CBI_TAG_BOARD_VERSION,
                  reinterpret_cast<const uint8_t*>(&kDefaultBoardVersion),
                  static_cast<uint8_t>(sizeof(kDefaultBoardVersion))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_OEM_ID,
                           reinterpret_cast<const uint8_t*>(&kDefaultOemId),
                           static_cast<uint8_t>(sizeof(kDefaultOemId))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_SKU_ID,
                           reinterpret_cast<const uint8_t*>(&kDefaultSkuId),
                           static_cast<uint8_t>(sizeof(kDefaultSkuId))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_MODEL_ID,
                           reinterpret_cast<const uint8_t*>(&kDefaultModelId),
                           static_cast<uint8_t>(sizeof(kDefaultModelId))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_FW_CONFIG,
                           reinterpret_cast<const uint8_t*>(&kDefaultFwConfig),
                           static_cast<uint8_t>(sizeof(kDefaultFwConfig))));

    ASSERT_EQ(0,
              cbi_set_board_info(
                  CBI_TAG_PCB_SUPPLIER,
                  reinterpret_cast<const uint8_t*>(&kDefaultPcbSupplier),
                  static_cast<uint8_t>(sizeof(kDefaultPcbSupplier))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_SSFC,
                           reinterpret_cast<const uint8_t*>(&kDefaultSsfc),
                           static_cast<uint8_t>(sizeof(kDefaultSsfc))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_REWORK_ID,
                           reinterpret_cast<const uint8_t*>(&kDefaultReworkId),
                           static_cast<uint8_t>(sizeof(kDefaultReworkId))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(
            CBI_TAG_FACTORY_CALIBRATION_DATA,
            reinterpret_cast<const uint8_t*>(&kDefaultFactoryCalibrationData),
            static_cast<uint8_t>(sizeof(kDefaultFactoryCalibrationData))));

    ASSERT_EQ(0,
              cbi_set_board_info(
                  CBI_TAG_DRAM_PART_NUM,
                  reinterpret_cast<const uint8_t*>(kDefaultDramPartNum),
                  static_cast<uint8_t>(std::strlen(kDefaultDramPartNum))));

    ASSERT_EQ(
        0,
        cbi_set_board_info(CBI_TAG_OEM_NAME,
                           reinterpret_cast<const uint8_t*>(kDefaultOemName),
                           static_cast<uint8_t>(std::strlen(kDefaultOemName))));

    gpio_callbacks_.handler = [](const struct device* port,
                                 struct gpio_callback*,
                                 gpio_port_pins_t pins) {
      while (pins != 0) {
        int pin = __builtin_ctz(pins);
        int value = gpio_emul_output_get(port, pin);
        SUSPEND();
        gpio_emul_input_set(
            kClientInterruptSpec.port, kClientInterruptSpec.pin, value);

        SUSPEND();
        pins &= ~BIT(pin);
      }
    };
    gpio_callbacks_.pin_mask = BIT(kServiceInterruptSpec.pin);

    int current_service_pin_value = gpio_emul_output_get(
        kServiceInterruptSpec.port, kServiceInterruptSpec.pin);

    gpio_emul_input_set(kClientInterruptSpec.port,
                        kClientInterruptSpec.pin,
                        current_service_pin_value);
    ASSERT_EQ(0,
              gpio_add_callback_dt(&kServiceInterruptSpec, &gpio_callbacks_));
  }

  void TearDown() override {
    gpio_remove_callback_dt(&kServiceInterruptSpec, &gpio_callbacks_);
  }

  void ClearTransport() {
    uint8_t data[CONFIG_PLATFORM_EC_DSP_RESPONSE_BUFFER_SIZE];

    k_msleep(1);
    i2c_read_dt(&kClientBusSpec, data, sizeof(data));
    i2c_read_dt(&kClientBusSpec, data, sizeof(data));
  }

  int SendServiceRequest(const cros_dsp_comms_EcService& service) {
    uint8_t buffer[cros_dsp_comms_EcService_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    PW_CHECK(pb_encode(&stream, cros_dsp_comms_EcService_fields, &service));

    printk("Writing %zu bytes\n", stream.bytes_written);
    return i2c_write_dt(&kClientBusSpec, buffer, stream.bytes_written);
  }

  pw_transport_Status ReadStatus() {
    pw_transport_Status status;
    uint8_t status_buffer[pw_transport_Status_size + 4] = {0};
    PW_CHECK_INT_EQ(
        0, i2c_read_dt(&kClientBusSpec, status_buffer, sizeof(status_buffer)));
    pb_istream_t istream =
        pb_istream_from_buffer(status_buffer, ARRAY_SIZE(status_buffer));
    PW_CHECK(
        pb_decode_delimited(&istream, pw_transport_Status_fields, &status));
    return status;
  }

  struct gpio_callback gpio_callbacks_ = {};
};

TEST_F(DspComms, ReadUnsupportedTag) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(-EINVAL,
            cbi_remote_get_board_info(
                CBI_TAG_COUNT, reinterpret_cast<uint8_t*>(&out), &size));
}

TEST_F(DspComms, FailToReadSmallBuffer) {
  uint8_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  EXPECT_EQ(
      -ENOMEM,
      cbi_remote_get_board_info(
          CBI_TAG_BOARD_VERSION, reinterpret_cast<uint8_t*>(&out), &size));

  EXPECT_EQ(-ENOMEM,
            cbi_remote_get_board_info(
                CBI_TAG_REWORK_ID, reinterpret_cast<uint8_t*>(&out), &size));

  EXPECT_EQ(
      -ENOMEM,
      cbi_remote_get_board_info(
          CBI_TAG_DRAM_PART_NUM, reinterpret_cast<uint8_t*>(&out), &size));
}

TEST_F(DspComms, TimeoutReadCbiValue) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  TearDown();
  ASSERT_EQ(
      -EAGAIN,
      cbi_remote_get_board_info(
          CBI_TAG_BOARD_VERSION, reinterpret_cast<uint8_t*>(&out), &size));
  k_msleep(CONFIG_PLATFORM_EC_DSP_CLIENT_TIMEOUT_MS * 2);
}

TEST_F(DspComms, SendTwoRequests) {
  cros_dsp_comms_EcService service = {
      .which_request = cros_dsp_comms_EcService_get_cbi_flags_tag,
      .request =
          {
              .get_cbi_flags =
                  {
                      .which = cros_dsp_comms_CbiFlag_VERSION,
                  },
          },
  };

  ASSERT_EQ(0, SendServiceRequest(service));
  ASSERT_EQ(-EIO, SendServiceRequest(service));
}

TEST_F(DspComms, ProcessingError) {
  // Create an invalid service request.
  ASSERT_EQ(-EINVAL,
            dsp_client_get_cbi_flags(
                kClient, static_cast<cros_dsp_comms_CbiFlag>(11), nullptr));
}

TEST_F(DspComms, ReadCbiVersion) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(
      0,
      cbi_remote_get_board_info(
          CBI_TAG_BOARD_VERSION, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultBoardVersion, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiOemId) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_OEM_ID, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultOemId, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiSkuId) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_SKU_ID, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultSkuId, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiModelId) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_MODEL_ID, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultModelId, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiFwConfig) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_FW_CONFIG, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultFwConfig, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiPcbSupplier) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_PCB_SUPPLIER, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultPcbSupplier, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiSsfc) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_SSFC, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultSsfc, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiReworkId) {
  uint64_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_REWORK_ID, reinterpret_cast<uint8_t*>(&out), &size));
  ASSERT_EQ(kDefaultReworkId, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiFactoryCalibrationData) {
  uint32_t out;
  uint8_t size = static_cast<uint8_t>(sizeof(out));

  ASSERT_EQ(0,
            cbi_remote_get_board_info(CBI_TAG_FACTORY_CALIBRATION_DATA,
                                      reinterpret_cast<uint8_t*>(&out),
                                      &size));
  ASSERT_EQ(kDefaultFactoryCalibrationData, out);
  ASSERT_EQ(size, static_cast<uint8_t>(sizeof(out)));
}

TEST_F(DspComms, ReadCbiDramPartNum) {
  char out[80];
  uint8_t size = sizeof(out);

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_DRAM_PART_NUM, reinterpret_cast<uint8_t*>(out), &size));
  ASSERT_STREQ(kDefaultDramPartNum, out);
  ASSERT_LE(size, sizeof(out));
}

TEST_F(DspComms, ReadCbiOemName) {
  char out[80];
  uint8_t size = sizeof(out);

  ASSERT_EQ(0,
            cbi_remote_get_board_info(
                CBI_TAG_OEM_NAME, reinterpret_cast<uint8_t*>(out), &size));
  ASSERT_STREQ(kDefaultOemName, out);
  ASSERT_LE(size, sizeof(out));
}

TEST_F(DspComms, LidPosition) {
  // Remove the callbacks for the GPIO. These callbacks forward the interrupt
  // from the service to the client. In this test, we want to manually do the
  // status read and assert the status values.
  gpio_remove_callback_dt(&kServiceInterruptSpec, &gpio_callbacks_);
  // Emulate lid open
  gpio_emul_input_set(kLidOpenInterruptSpec.port, kLidOpenInterruptSpec.pin, 1);
  // Emulate clamshell mode
  tablet_set_mode(0, TABLET_TRIGGER_LID);
  hook_notify(HOOK_INIT);
  auto status = ReadStatus();
  ASSERT_TRUE(
      IsFlagSet(status, cros_dsp_comms_StatusFlag_STATUS_FLAG_LID_OPEN));
  ASSERT_FALSE(
      IsFlagSet(status, cros_dsp_comms_StatusFlag_STATUS_FLAG_TABLET_MODE));
}

}  // namespace
