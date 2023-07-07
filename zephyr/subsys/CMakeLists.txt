# SPDX-License-Identifier: Apache-2.0

add_subdirectory(ap_pwrseq)
add_subdirectory(emul)
add_subdirectory_ifdef(CONFIG_HOST_SERVICE host_service)
