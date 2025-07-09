# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_USB_DEVICE_STACK_NEXT usbd_init.c)

if (CONFIG_CROS_EC_RW)
  zephyr_library_sources_ifdef(CONFIG_USBD_HID_KEYBOARD usbd_hid_kb.c)
  zephyr_library_sources_ifdef(CONFIG_USBD_HID_TOUCHPAD usbd_hid_tp.c)
  zephyr_library_sources_ifdef(CONFIG_USBD_HID_VIVALDI hid_vivaldi.c)
else()
  # RO does not care about keyboard and trackpad, but updater only
  zephyr_library_sources_ifdef(CONFIG_USBD_HID_KEYBOARD usbd_google_fake.c)
  zephyr_library_sources_ifdef(CONFIG_USBD_HID_TOUCHPAD usbd_google_fake.c)
endif()

zephyr_library_sources_ifdef(CONFIG_USBD_GOOGLE_UPDATE_SUPPORT usbd_google_update.c)
zephyr_library_sources_ifdef(CONFIG_USBD_GOOGLE_I2C_SUPPORT usbd_google_i2c.c)
