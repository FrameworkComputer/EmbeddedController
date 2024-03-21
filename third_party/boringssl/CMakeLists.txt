# Copyright (c) 2023 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_BORINGSSL_CRYPTO)

zephyr_library_named(boringssl)

# Currently BoringSSL is not as a module, so we need to provide a path to
# sources.

# Look for BoringSSL sources in MODULES_DIR if provided.
if (DEFINED ENV{MODULES_DIR})
	set(BORINGSSL_SOURCES $ENV{MODULES_DIR}/boringssl)
# If MODULES_DIR is not available, look for BoringSSL in modules/boringssl
# which should be correct in Gitlab CI.
elseif(EXISTS ${ZEPHYR_BASE}/../../modules/boringssl)
	set(BORINGSSL_SOURCES ${ZEPHYR_BASE}/../../modules/boringssl)
# When releasing FPMCU firmware, the directory structure is
#    |_ zephyr-base/
#    |_ modules/
#    |   |_ boringssl/
#
# Check ${ZEPHYR_BASE}/../modules/boringssl for BoringSSL sources.
elseif(EXISTS ${ZEPHYR_BASE}/../modules/boringssl)
	set(BORINGSSL_SOURCES ${ZEPHYR_BASE}/../modules/boringssl)
# Finally, look for BoringSSL in third_part/boringssl which should be correct
# when running in cros_sdk.
else()
	set(BORINGSSL_SOURCES ${ZEPHYR_BASE}/../../boringssl)
endif()

# Enable BoringSSL's checks if assert is enabled.
if (DEFINED CONFIG_ASSERT)
	set(CMAKE_BUILD_TYPE RelWithAsserts)
else()
	set(CMAKE_BUILD_TYPE Release)
endif()

# TODO(b/273639386): Remove these workarounds when the upstream supports
# better way to disable the filesystem, threads and locks usages.
set(CMAKE_SYSTEM_NAME Linux)
set(ANDROID TRUE)
zephyr_compile_definitions(__TRUSTY__)

# Zephyr defines _XOPEN_SOURCE=600 when compiling 'posix' architecture,
# but BoringSSL defines _XOPEN_SOURCE=700. Since redefining is not allowed
# in compiler command line, undefine _XOPEN_SOURCE.
if (DEFINED CONFIG_ARCH_POSIX)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -U_XOPEN_SOURCE")
endif()

# We are not going to install BoringSSL, so don't generate install rules.
set(CMAKE_SKIP_INSTALL_RULES YES)

# We don't use generated assembly for now.
set(OPENSSL_NO_ASM "1")

zephyr_include_directories(${BORINGSSL_SOURCES})
zephyr_include_directories(${BORINGSSL_SOURCES}/include)
zephyr_include_directories(include)

# Add CMakeLists.txt from BoringSSL. This directory is out-of-tree, so we need
# to specify binary directory also. BoringSSL defines many CMake targets which
# we don't want to compile (e.g. tests), so we provide EXCLUDE_FROM_ALL
# argument also. EXCLUDE_FROM_ALL also excludes 'crypto' target, but CMake
# will build it because we will set dependency on that.
add_subdirectory(${BORINGSSL_SOURCES} build EXCLUDE_FROM_ALL)

# Link 'crypto' library into boringssl library.
zephyr_library_link_libraries(crypto)

# Provide implementation of CRYPTO_sysrand() using Zephyr Entropy Device.
zephyr_library_sources_ifdef(CONFIG_BORINGSSL_HARDWARE_ENTROPY
	common/sysrand_zephyr.c)

# Change language to C11 because sysrand_zephyr.c includes header from
# BoringSSL which requires that.
zephyr_library_compile_options(-std=c11)

# Add compile options required by BoringSSL.
# -Wno-implicit-function-declaration is needed because BoringSSL sources use
# functions not defined in Zephyr: fflush(), getenv(), sscanf(). We won't get
# any linker issues because code that uses these functions is unused and will
# be removed by -ffunction-sections and -Wl,--gc-sections.
#
# -fno-common disables merging tentative definitions (uninitialized global
# variables) into pre-existing definition (initialized global variable).
# Recommended by BoringSSL.
#
# -std=c11 is necessary because BoringSSL sources use features available in
# C11, e.g. static_assert().
set(BORINGSSL_COMPILE_OPTIONS -Wno-implicit-function-declaration -fno-common -std=c11)

# Target 'fipsmodule' is a object library. It compiles source files, but
# doesn't archive or link object files into library. Other targets should
# reference objects from this target with $<TARGET_OBJECTS:fipsmodule>.
# Sources from the target are compiled with the object library flags, not
# flags from target that reference objects. As a result we need to add compile
# options to 'fipsmodule' separately.
set(ZEPHYR_CURRENT_LIBRARY fipsmodule)
target_link_libraries(fipsmodule PUBLIC zephyr_interface)

# -Wno-return-type is needed to ignore non-void functions that calls abort() and
# don't have return statement (e.g. aes_hw_set_encrypt_key()).
zephyr_library_compile_options(-Wno-return-type ${BORINGSSL_COMPILE_OPTIONS})

# Now we can set compile options for 'crypto' library.
set(ZEPHYR_CURRENT_LIBRARY crypto)
target_link_libraries(crypto PUBLIC zephyr_interface)

# Add recommended compile options.
zephyr_library_compile_options(${BORINGSSL_COMPILE_OPTIONS})

# Enable LTO if enabled for 'app' library.
get_property(APP_LTO_PROPERTY TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION)
set_property(TARGET boringssl PROPERTY INTERPROCEDURAL_OPTIMIZATION
	${APP_LTO_PROPERTY})
set_property(TARGET crypto PROPERTY INTERPROCEDURAL_OPTIMIZATION
	${APP_LTO_PROPERTY})
set_property(TARGET fipsmodule PROPERTY INTERPROCEDURAL_OPTIMIZATION
	${APP_LTO_PROPERTY})

endif()
