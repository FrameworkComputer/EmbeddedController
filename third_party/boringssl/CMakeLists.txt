# Copyright (c) 2023 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_BORINGSSL_CRYPTO)

zephyr_interface_library_named(boringssl)

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

# We are not going to install BoringSSL, so don't generate install rules.
set(CMAKE_SKIP_INSTALL_RULES YES)

# We don't use generated assembly for now.
set(OPENSSL_NO_ASM "1")

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

# Add compile options required by BoringSSL.
# -D_POSIX_C_SOURCE=200809L is needed because BoringSSL uses strdup() function
# is guarded by __POSIX_VISIBLE >=200809 in Newlib's string.h file.
#
# -DPTHREAD_RWLOCK_INITIALIZER=POSIX_RWLOCK_INITIALIZER is due to an
# inconsistent naming of the rwlock initialization macro in the Zephyr posix
# implementation.
#
# -Wno-implicit-function-declaration is needed because BoringSSL sources use
# functions not defined in Zephyr: fflush(), getenv(), sscanf(). We won't get
# any linker issues because code that uses these functions is unused and will
# be removed by -ffunction-sections and -Wl,--gc-sections.
#
# -fno-common disables merging tentative definitions (uninitialized global
# variables) into pre-existing definition (initialized global variable).
# Recommended by BoringSSL.
set(BORINGSSL_COMPILE_OPTIONS
	-D_POSIX_C_SOURCE=200809L
	-DPTHREAD_RWLOCK_INITIALIZER=POSIX_RWLOCK_INITIALIZER
	-Wno-implicit-function-declaration
	-fno-common
)

# Create interface library that specifies compile options required by
# BoringSSL. This is the same trick used by zephyr_library_compile_options()
# macro to make sure that order of compilation options is correct.
#
# We can't use the macro, because it uses target_link_libraries() with keyword
# signature (like we do for 'fipsmodule' below) and BoringSSL uses the plain
# signature for 'crypto' target. CMake forbids to mix plain signature and
# keyword signature for target. That's also the reason why we don't specify
# PUBLIC or PRIVATE when linking libraries to 'crypto' target.
add_library(boringssl_compile_options_lib INTERFACE)
target_compile_options(boringssl_compile_options_lib INTERFACE
	${BORINGSSL_COMPILE_OPTIONS})

# Target 'fipsmodule' is a object library. It compiles source files, but
# doesn't archive or link object files into library. Other targets should
# reference objects from this target with $<TARGET_OBJECTS:fipsmodule>.
# Sources from the target are compiled with the object library flags, not
# flags from target that reference objects. As a result we need to add compile
# options to 'fipsmodule' separately.
target_link_libraries(fipsmodule PUBLIC zephyr_interface)
target_link_libraries(fipsmodule PRIVATE boringssl_compile_options_lib)

# Add recommended compile options for 'crypto' library.
target_link_libraries(crypto zephyr_interface boringssl_compile_options_lib)

# Enable LTO if enabled for 'app' library.
get_property(APP_LTO_PROPERTY TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION)
set_property(TARGET boringssl PROPERTY INTERPROCEDURAL_OPTIMIZATION
	${APP_LTO_PROPERTY})
set_property(TARGET crypto PROPERTY INTERPROCEDURAL_OPTIMIZATION
	${APP_LTO_PROPERTY})
set_property(TARGET fipsmodule PROPERTY INTERPROCEDURAL_OPTIMIZATION
	${APP_LTO_PROPERTY})

endif()
