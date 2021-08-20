# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_EMUL_SMART_BATTERY	emul_smart_battery.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BMA255		emul_bma255.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BC12_DETECT_PI3USB9201	emul_pi3usb9201.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_PPC_SYV682X	emul_syv682x.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BMI		emul_bmi.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BMI		emul_bmi160.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BMI		emul_bmi260.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_TCS3400	emul_tcs3400.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BB_RETIMER	emul_bb_retimer.c)
