# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_EMUL_TCPCI		emul_tcpci.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_PS8XXX		emul_ps8xxx.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_TCPCI_PARTNER_SRC	emul_tcpci_partner_src.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_TCPCI_PARTNER_COMMON	emul_tcpci_partner_common.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_TCPCI_PARTNER_SNK	emul_tcpci_partner_snk.c)
