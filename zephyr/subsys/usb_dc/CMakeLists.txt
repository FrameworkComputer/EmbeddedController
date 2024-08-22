# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_USB_DEVICE_STACK usb_dc.c)
if (CONFIG_CROS_EC_RW)
  zephyr_library_sources_ifdef(CONFIG_USB_DC_HID_KEYBOARD usb_hid_kb.c)
  zephyr_library_sources_ifdef(CONFIG_USB_DC_HID_TOUCHPAD usb_hid_tp.c)
  zephyr_library_sources_ifdef(CONFIG_USB_DC_HID_VIVALDI hid_vivaldi.c)
else()
  # RO does not care about keyboard and trackpad, but updater only
  zephyr_library_sources_ifdef(CONFIG_USB_DC_HID_KEYBOARD usb_hid_kb_dummy.c usb_google_fake.c)
  zephyr_library_sources_ifdef(CONFIG_USB_DC_HID_TOUCHPAD usb_hid_tp_dummy.c usb_google_fake.c)
endif ()

zephyr_library_sources_ifdef(CONFIG_USB_DEVICE_GOOGLE_UPDATE usb_google_update.c)
zephyr_library_sources_ifdef(CONFIG_USB_DEVICE_GOOGLE_I2C usb_google_i2c.c)
