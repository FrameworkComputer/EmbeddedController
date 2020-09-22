# SPDX-License-Identifier: Apache-2.0

# find ECST tool for generating npcx header used by ROM code

# This calls into a helper script in the npcx eval board directory; this script
# should be moved into a better location (e.g. chip series directory)

set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
  COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/boards/arm/npcx7m6fb_evb/support/ecst.py
  -i ${PROJECT_BINARY_DIR}/${CONFIG_KERNEL_BIN_NAME}.bin
  -o ${TARGET_IMAGE_FILE} -nohcrc -nofcrc
  -chip npcx7m6 -flashsize 8 -spimaxclk 50 -spireadmode dual
)
