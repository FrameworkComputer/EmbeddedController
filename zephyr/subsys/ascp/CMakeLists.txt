# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources(claim.c)
zephyr_library_sources_ifdef(CONFIG_ASCP_MEM_MAP mem_map.c)

if(CONFIG_ASCP_REPLACE_S_GOOG)
  # Look for the binary files in the application source directory, the
  # directory of the board definition, and all directories where configuration
  # files are located.
  set(ascp_inc_hints ${APPLICATION_SOURCE_DIR})
  if(DEFINED BOARD_DIR)
    list(APPEND ascp_inc_hints ${BOARD_DIR})
  endif()
  if(DEFINED CONF_FILE)
    foreach(conf ${CONF_FILE})
      get_filename_component(conf_dir ${conf} DIRECTORY)
      list(APPEND ascp_inc_hints ${conf_dir})
    endforeach()
  endif()
  list(REMOVE_DUPLICATES ascp_inc_hints)

  function(ascp_generate_inc source_var output_name)
    set(filename ${${source_var}})
    find_file(${source_var}_PATH NAMES ${filename} HINTS ${ascp_inc_hints} NO_DEFAULT_PATH)
    if(${source_var}_PATH)
      set(full_path ${${source_var}_PATH})
    else()
      set(full_path ${filename})
    endif()
    generate_inc_file_for_target(app ${full_path} ${CMAKE_CURRENT_BINARY_DIR}/${output_name})
  endfunction()

  ascp_generate_inc(CONFIG_ASCP_S_GOOG_MATCH_FILE s_goog_match.inc)
  ascp_generate_inc(CONFIG_ASCP_S_GOOG_REPLACE_FILE s_goog_replace.inc)

  zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR})
endif()
