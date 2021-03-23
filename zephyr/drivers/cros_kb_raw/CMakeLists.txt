# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_NPCX	cros_kb_raw_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_ITE	cros_kb_raw_ite.c)
